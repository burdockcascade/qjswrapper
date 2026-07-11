#pragma once

#include <quickjs.h>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <stdexcept>
#include <utility>
#include <unordered_map>
#include <vector>
#include <shared_mutex>
#include <mutex>

#include "value.hpp"

namespace qjs {

    namespace detail {

        template <typename T>
        JSValue class_constructor_dispatcher(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
            if (JS_IsUndefined(new_target)) {
                return JS_ThrowTypeError(ctx, "Constructor must be called with 'new'");
            }

            JSRuntime* rt = JS_GetRuntime(ctx);
            std::vector<std::function<T*(JSContext*, int, JSValueConst*)>> ctors;
            JSClassID class_id = 0;

            {
                std::shared_lock lock(JSClassInfo<T>::mutex);
                if (JSClassInfo<T>::constructors.contains(rt)) {
                    ctors = JSClassInfo<T>::constructors.at(rt);
                }
                if (JSClassInfo<T>::ids.contains(rt)) {
                    class_id = JSClassInfo<T>::ids.at(rt);
                }
            }

            if (ctors.empty()) {
                return JS_ThrowTypeError(ctx, "No constructors defined for class");
            }

            T* instance = nullptr;
            for (const auto& ctor : ctors) {
                instance = ctor(ctx, argc, argv);
                if (instance) break;
            }

            if (!instance) {
                return JS_ThrowTypeError(ctx, "No matching constructor found for the provided arguments.");
            }

            JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
            if (JS_IsException(proto)) {
                delete instance;
                return proto;
            }

            if (class_id == 0) {
                delete instance;
                JS_FreeValue(ctx, proto);
                return JS_ThrowTypeError(ctx, "Class not registered in runtime");
            }

            JSValue obj = JS_NewObjectProtoClass(ctx, proto, class_id);
            JS_FreeValue(ctx, proto);

            if (JS_IsException(obj)) {
                delete instance;
                return obj;
            }

            JS_SetOpaque(obj, instance);
            return obj;
        }

        // Direct member variable getter
        template <typename T, typename M>
        JSValue class_getter(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* func_data) {
            JSRuntime* rt = JS_GetRuntime(ctx);
            JSClassID class_id = 0;

            {
                std::shared_lock lock(JSClassInfo<T>::mutex);
                if (JSClassInfo<T>::ids.contains(rt)) {
                    class_id = JSClassInfo<T>::ids.at(rt);
                }
            }

            if (class_id == 0) return JS_ThrowTypeError(ctx, "Class not registered in runtime");

            T* ptr = static_cast<T*>(JS_GetOpaque(this_val, class_id));
            if (!ptr) return JS_ThrowTypeError(ctx, "Invalid 'this' pointer");

            size_t size;
            uint8_t* data = JS_GetArrayBuffer(ctx, &size, func_data[0]);
            if (!data) return JS_ThrowInternalError(ctx, "Failed to retrieve member pointer from ArrayBuffer");

            using MemberPtr = M T::*;
            MemberPtr member = *reinterpret_cast<MemberPtr*>(data);

            return Value::create_js_value(ctx, ptr->*member);
        }

        // Direct member variable setter
        template <typename T, typename M>
        JSValue class_setter(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* func_data) {
            JSRuntime* rt = JS_GetRuntime(ctx);
            JSClassID class_id = 0;

            {
                std::shared_lock lock(JSClassInfo<T>::mutex);
                if (JSClassInfo<T>::ids.contains(rt)) {
                    class_id = JSClassInfo<T>::ids.at(rt);
                }
            }

            if (class_id == 0) return JS_ThrowTypeError(ctx, "Class not registered in runtime");

            T* ptr = static_cast<T*>(JS_GetOpaque(this_val, class_id));
            if (!ptr) return JS_ThrowTypeError(ctx, "Invalid 'this' pointer");

            if (argc > 0) {
                size_t size;
                uint8_t* data = JS_GetArrayBuffer(ctx, &size, func_data[0]);
                if (!data) return JS_ThrowInternalError(ctx, "Failed to retrieve member pointer from ArrayBuffer");

                using MemberPtr = M T::*;
                MemberPtr member = *reinterpret_cast<MemberPtr*>(data);

                auto val_opt = Value(ctx, JS_DupValue(ctx, argv[0])).template as<std::decay_t<M>>();
                if (val_opt) {
                    ptr->*member = *val_opt;
                } else {
                    return JS_ThrowTypeError(ctx, "Invalid type assigned to property");
                }
            }
            return JS_UNDEFINED;
        }

        // Container object to capture member function pointers together
        template <typename T, typename GetterR, typename SetterArgs>
        struct PropertyMethods {
            GetterR (T::*getter)() const;
            void (T::*setter)(SetterArgs);
        };

        // Custom trampoline for functional getters
        template <typename T, typename GetterR, typename SetterArgs>
        JSValue class_getter_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* func_data) {
            JSRuntime* rt = JS_GetRuntime(ctx);
            JSClassID class_id = 0;

            {
                std::shared_lock lock(JSClassInfo<T>::mutex);
                if (JSClassInfo<T>::ids.contains(rt)) {
                    class_id = JSClassInfo<T>::ids.at(rt);
                }
            }

            if (class_id == 0) return JS_ThrowTypeError(ctx, "Class not registered in runtime");

            T* ptr = static_cast<T*>(JS_GetOpaque(this_val, class_id));
            if (!ptr) return JS_ThrowTypeError(ctx, "Invalid 'this' pointer");

            size_t size;
            uint8_t* data = JS_GetArrayBuffer(ctx, &size, func_data[0]);
            if (!data) return JS_ThrowInternalError(ctx, "Failed to retrieve methods from ArrayBuffer");

            using MethodsType = PropertyMethods<T, GetterR, SetterArgs>;
            MethodsType* methods = reinterpret_cast<MethodsType*>(data);

            if (!methods->getter) {
                return JS_ThrowTypeError(ctx, "Property is write-only");
            }

            try {
                return Value::create_js_value(ctx, (ptr->*(methods->getter))());
            } catch (const std::exception& e) {
                return JS_ThrowInternalError(ctx, "%s", e.what());
            }
        }

        // Custom trampoline for functional setters
        template <typename T, typename GetterR, typename SetterArgs>
        JSValue class_setter_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* func_data) {
            JSRuntime* rt = JS_GetRuntime(ctx);
            JSClassID class_id = 0;

            {
                std::shared_lock lock(JSClassInfo<T>::mutex);
                if (JSClassInfo<T>::ids.contains(rt)) {
                    class_id = JSClassInfo<T>::ids.at(rt);
                }
            }

            if (class_id == 0) return JS_ThrowTypeError(ctx, "Class not registered in runtime");

            T* ptr = static_cast<T*>(JS_GetOpaque(this_val, class_id));
            if (!ptr) return JS_ThrowTypeError(ctx, "Invalid 'this' pointer");

            size_t size;
            uint8_t* data = JS_GetArrayBuffer(ctx, &size, func_data[0]);
            if (!data) return JS_ThrowInternalError(ctx, "Failed to retrieve methods from ArrayBuffer");

            using MethodsType = PropertyMethods<T, GetterR, SetterArgs>;
            MethodsType* methods = reinterpret_cast<MethodsType*>(data);

            if (!methods->setter) {
                return JS_ThrowTypeError(ctx, "Property is read-only");
            }

            if (argc > 0) {
                auto val_opt = Value(ctx, JS_DupValue(ctx, argv[0])).template as<std::decay_t<SetterArgs>>();
                if (val_opt) {
                    try {
                        (ptr->*(methods->setter))(*val_opt);
                    } catch (const std::exception& e) {
                        return JS_ThrowInternalError(ctx, "%s", e.what());
                    }
                } else {
                    return JS_ThrowTypeError(ctx, "Invalid type assigned to property");
                }
            }
            return JS_UNDEFINED;
        }

        // Standard method dispatcher
        template <typename T, typename R, typename... Args>
        JSValue class_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* func_data) {
            JSRuntime* rt = JS_GetRuntime(ctx);
            JSClassID class_id = 0;

            {
                std::shared_lock lock(JSClassInfo<T>::mutex);
                if (JSClassInfo<T>::ids.contains(rt)) {
                    class_id = JSClassInfo<T>::ids.at(rt);
                }
            }

            if (class_id == 0) return JS_ThrowTypeError(ctx, "Class not registered in runtime");

            T* ptr = static_cast<T*>(JS_GetOpaque(this_val, class_id));
            if (!ptr) return JS_ThrowTypeError(ctx, "Invalid 'this' pointer");

            size_t size;
            uint8_t* data = JS_GetArrayBuffer(ctx, &size, func_data[0]);
            if (!data) return JS_ThrowInternalError(ctx, "Failed to retrieve method pointer from ArrayBuffer");

            using MethodPtr = R (T::*)(Args...);
            MethodPtr method = *reinterpret_cast<MethodPtr*>(data);

            try {
                auto invoke = [&]<std::size_t... I>(std::index_sequence<I...>) {
                    using ArgsTuple = std::tuple<std::decay_t<Args>...>;
                    if constexpr (std::is_void_v<R>) {
                        (ptr->*method)(
                            (I < static_cast<std::size_t>(argc) ?
                                Value(ctx, JS_DupValue(ctx, argv[I]))
                                    .template as<std::tuple_element_t<I, ArgsTuple>>()
                                    .value_or(std::tuple_element_t<I, ArgsTuple>{})
                                : std::tuple_element_t<I, ArgsTuple>{})...
                        );
                        return JS_UNDEFINED;
                    } else {
                        return Value::create_js_value(ctx, (ptr->*method)(
                            (I < static_cast<std::size_t>(argc) ?
                                Value(ctx, JS_DupValue(ctx, argv[I]))
                                    .template as<std::tuple_element_t<I, ArgsTuple>>()
                                    .value_or(std::tuple_element_t<I, ArgsTuple>{})
                                : std::tuple_element_t<I, ArgsTuple>{})...
                        ));
                    }
                };
                return invoke(std::index_sequence_for<Args...>{});
            } catch (const std::exception& e) {
                return JS_ThrowInternalError(ctx, "%s", e.what());
            }
        }
    }

    // 3. The Fluent Builder
    template <typename T>
    class ClassBuilder {
    public:
        ClassBuilder(JSContext* ctx, const std::string_view name)
            : ctx(ctx), name(name) {
            proto = JS_NewObject(ctx);
        }

        ClassBuilder(const ClassBuilder&) = delete;
        ClassBuilder& operator=(const ClassBuilder&) = delete;

        ClassBuilder(ClassBuilder&& other) noexcept
            : ctx(std::exchange(other.ctx, nullptr)),
              name(std::move(other.name)),
              proto(std::exchange(other.proto, JS_UNDEFINED)),
              ctor(std::exchange(other.ctor, JS_UNDEFINED)) {}

        ~ClassBuilder() {
            if (!ctx) return;

            if (JS_IsUndefined(ctor)) {
                ctor = JS_NewCFunction(ctx, [](JSContext* c, JSValueConst, int, JSValueConst*) {
                    return JS_ThrowTypeError(c, "Class cannot be instantiated directly");
                }, name.c_str(), 0);
            }

            JS_SetConstructor(ctx, ctor, proto);

            JSRuntime* rt = JS_GetRuntime(ctx);
            JSClassID class_id = 0;

            // FIX: Thread-safe read during shutdown
            {
                std::shared_lock lock(detail::JSClassInfo<T>::mutex);
                if (detail::JSClassInfo<T>::ids.contains(rt)) {
                    class_id = detail::JSClassInfo<T>::ids.at(rt);
                }
            }

            if (class_id != 0) {
                JS_SetClassProto(ctx, class_id, proto);
            }

            JSValue global = JS_GetGlobalObject(ctx);
            JS_SetPropertyStr(ctx, global, name.c_str(), ctor);
            JS_FreeValue(ctx, global);
        }

        template <typename... Args>
        ClassBuilder& add_constructor() {
            JSRuntime* rt = JS_GetRuntime(ctx);

            {
                std::unique_lock lock(detail::JSClassInfo<T>::mutex);
                detail::JSClassInfo<T>::constructors[rt].push_back([](JSContext* c, int argc, JSValueConst* argv) -> T* {
                    if (argc != sizeof...(Args)) return nullptr;

                    bool match = true;
                    if constexpr (sizeof...(Args) > 0) {
                        auto check = [&]<std::size_t... I>(std::index_sequence<I...>) {
                            using ArgsTuple = std::tuple<std::decay_t<Args>...>;
                            return (Value(c, JS_DupValue(c, argv[I])).template as<std::tuple_element_t<I, ArgsTuple>>().has_value() && ...);
                        };
                        match = check(std::index_sequence_for<Args...>{});
                    }
                    if (!match) return nullptr;

                    auto construct = [&]<std::size_t... I>(std::index_sequence<I...>) {
                        using ArgsTuple = std::tuple<std::decay_t<Args>...>;
                        return new T(
                            Value(c, JS_DupValue(c, argv[I])).template as<std::tuple_element_t<I, ArgsTuple>>().value()...
                        );
                    };
                    return construct(std::index_sequence_for<Args...>{});
                });
            }

            if (!JS_IsUndefined(ctor)) JS_FreeValue(ctx, ctor);

            ctor = JS_NewCFunction2(ctx, detail::class_constructor_dispatcher<T>, name.c_str(), 0, JS_CFUNC_constructor, 0);
            return *this;
        }

        // Original direct member variable tracking
        template <typename M>
        ClassBuilder& add_property(const std::string_view prop_name, M T::* member) {
            using MemberPtr = M T::*;
            MemberPtr* mem_ptr = new MemberPtr(member);

            JSValue data_obj = JS_NewArrayBuffer(
                ctx,
                reinterpret_cast<uint8_t*>(mem_ptr),
                sizeof(MemberPtr),
                detail::lambda_free_func<MemberPtr>,
                nullptr,
                false
            );

            if (JS_IsException(data_obj)) {
                delete mem_ptr;
                throw std::runtime_error("ClassBuilder: Failed to allocate ArrayBuffer for property.");
            }

            JSValue getter = JS_NewCFunctionData(ctx, detail::class_getter<T, M>, 0, 0, 1, &data_obj);
            JSValue setter = JS_NewCFunctionData(ctx, detail::class_setter<T, M>, 1, 0, 1, &data_obj);

            JSAtom atom = JS_NewAtomLen(ctx, prop_name.data(), prop_name.size());
            JS_DefinePropertyGetSet(ctx, proto, atom, getter, setter, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);

            JS_FreeAtom(ctx, atom);
            JS_FreeValue(ctx, data_obj);

            return *this;
        }

        // --- NEW: Overload accepting explicit Getter and Setter method pointers ---
        template <typename GetterR, typename SetterArgs>
        ClassBuilder& add_property(const std::string_view prop_name, GetterR (T::*getter)() const, void (T::*setter)(SetterArgs)) {
            using MethodsType = detail::PropertyMethods<T, GetterR, SetterArgs>;
            MethodsType* methods = new MethodsType{getter, setter};

            // Capture the method pointers inside an ArrayBuffer managed by QuickJS
            JSValue data_obj = JS_NewArrayBuffer(
                ctx,
                reinterpret_cast<uint8_t*>(methods),
                sizeof(MethodsType),
                detail::lambda_free_func<MethodsType>,
                nullptr,
                false
            );

            if (JS_IsException(data_obj)) {
                delete methods;
                throw std::runtime_error("ClassBuilder: Failed to allocate ArrayBuffer for method property.");
            }

            // Instantiating trampolines with type parameters matching our methods
            JSValue js_getter = JS_NewCFunctionData(ctx, detail::class_getter_method<T, GetterR, SetterArgs>, 0, 0, 1, &data_obj);
            JSValue js_setter = JS_NewCFunctionData(ctx, detail::class_setter_method<T, GetterR, SetterArgs>, 1, 0, 1, &data_obj);

            JSAtom atom = JS_NewAtomLen(ctx, prop_name.data(), prop_name.size());
            JS_DefinePropertyGetSet(ctx, proto, atom, js_getter, js_setter, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);

            JS_FreeAtom(ctx, atom);
            JS_FreeValue(ctx, data_obj);

            return *this;
        }

        template <typename R, typename... Args>
        ClassBuilder& add_method(const std::string_view method_name, R (T::*method)(Args...)) {
            using MethodPtr = R (T::*)(Args...);
            MethodPtr* mem_ptr = new MethodPtr(method);

            JSValue data_obj = JS_NewArrayBuffer(
                ctx,
                reinterpret_cast<uint8_t*>(mem_ptr),
                sizeof(MethodPtr),
                detail::lambda_free_func<MethodPtr>,
                nullptr,
                false
            );

            if (JS_IsException(data_obj)) {
                delete mem_ptr;
                throw std::runtime_error("ClassBuilder: Failed to allocate ArrayBuffer for method.");
            }

            JSValue js_func = JS_NewCFunctionData(ctx, detail::class_method<T, R, Args...>, sizeof...(Args), 0, 1, &data_obj);

            JS_SetPropertyStr(ctx, proto, method_name.data(), js_func);
            JS_FreeValue(ctx, data_obj);

            return *this;
        }

        [[nodiscard]] Value build() {
            if (!ctx || JS_IsUndefined(proto)) {
                throw std::runtime_error("ClassBuilder: class already built or invalid state.");
            }

            if (JS_IsUndefined(ctor)) {
                ctor = JS_NewCFunction(ctx, [](JSContext* c, JSValueConst, int, JSValueConst*) {
                    return JS_ThrowTypeError(c, "Class cannot be instantiated directly");
                }, name.c_str(), 0);
            }

            JS_SetConstructor(ctx, ctor, proto);

            JSRuntime* rt = JS_GetRuntime(ctx);
            JSClassID class_id = 0;

            {
                std::shared_lock lock(detail::JSClassInfo<T>::mutex);
                if (detail::JSClassInfo<T>::ids.contains(rt)) {
                    class_id = detail::JSClassInfo<T>::ids.at(rt);
                }
            }

            if (class_id != 0) {
                JS_SetClassProto(ctx, class_id, proto);
            }

            proto = JS_UNDEFINED;
            JSValue result_ctor = ctor;
            ctor = JS_UNDEFINED;

            return Value(ctx, result_ctor);
        }

    private:
        JSContext* ctx;
        std::string name;
        JSValue proto;
        JSValue ctor{JS_UNDEFINED};
    };

} // namespace qjs