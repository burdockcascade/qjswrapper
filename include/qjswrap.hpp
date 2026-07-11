/* Auto Generated */
/* Amalgamated Header */
#pragma once

// System Includes
#include <concepts>
#include <cstring>
#include <expected>
#include <format>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <quickjs.h>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>











       // FIX: Added for std::strlen and std::memcpy
  // FIX: Added for thread safety
         // FIX: Added for thread safety
















namespace qjs {

    namespace detail {
        // Thread-safe class info registry
        template <typename T>
        struct JSClassInfo {
            static inline std::shared_mutex mutex;
            static inline std::unordered_map<JSRuntime*, JSClassID> ids;
            static inline std::string name;
            static inline std::unordered_map<JSRuntime*, std::vector<std::function<T*(JSContext*, int, JSValueConst*)>>> constructors;
        };

        // Function Traits: Deduces Return Types and Arguments from Callables
        template <typename T>
        struct function_traits : public function_traits<decltype(&T::operator())> {};

        template <typename ClassType, typename ReturnType, typename... Args>
        struct function_traits<ReturnType(ClassType::*)(Args...) const> {
            using result_type = ReturnType;
            using args_tuple = std::tuple<std::decay_t<Args>...>;
            static constexpr std::size_t arity = sizeof...(Args);
        };

        template <typename ClassType, typename ReturnType, typename... Args>
        struct function_traits<ReturnType(ClassType::*)(Args...)> {
            using result_type = ReturnType;
            using args_tuple = std::tuple<std::decay_t<Args>...>;
            static constexpr std::size_t arity = sizeof...(Args);
        };

        template <typename ReturnType, typename... Args>
        struct function_traits<ReturnType(*)(Args...)> {
            using result_type = ReturnType;
            using args_tuple = std::tuple<std::decay_t<Args>...>;
            static constexpr std::size_t arity = sizeof...(Args);
        };

        template <typename F>
        void lambda_free_func(JSRuntime* rt, void* opaque, void* ptr) {
            delete static_cast<F*>(ptr);
        }

        template <typename F>
        JSValue cpp_closure_trampoline(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* func_data);
    }

    // Broadened to accept any integral, floating point, string, or class
    template <typename T>
    concept JSSupportedType = std::is_integral_v<std::decay_t<T>> ||
                              std::is_floating_point_v<std::decay_t<T>> ||
                              std::is_same_v<std::decay_t<T>, std::string> ||
                              std::convertible_to<std::decay_t<T>, std::string_view> ||
                              std::is_class_v<std::decay_t<T>>;

    class Value {
    public:
        Value() = default;
        Value(JSContext* ctx, const JSValue val) : ctx(ctx), val(val) {}

        template <JSSupportedType T>
        Value(JSContext* ctx, T&& v) : ctx(ctx) {
            val = create_js_value(ctx, std::forward<T>(v));
        }

        ~Value() {
            if (ctx) {
                JS_FreeValue(ctx, val);
            }
        }

        Value(const Value& other) : ctx(other.ctx) {
            val = JS_DupValue(ctx, other.val);
        }

        Value& operator=(const Value& other) {
            if (this != &other) {
                if (ctx) JS_FreeValue(ctx, val);
                ctx = other.ctx;
                val = JS_DupValue(ctx, other.val);
            }
            return *this;
        }

        Value(Value&& other) noexcept
            : ctx(std::exchange(other.ctx, nullptr)),
              val(std::exchange(other.val, JS_UNDEFINED)) {}

        Value& operator=(Value&& other) noexcept {
            if (this != &other) {
                if (ctx) JS_FreeValue(ctx, val);
                ctx = std::exchange(other.ctx, nullptr);
                val = std::exchange(other.val, JS_UNDEFINED);
            }
            return *this;
        }

        // Converts native C++ types OR existing Values into QuickJS JSValues
        template <typename T>
        static JSValue create_js_value(JSContext* context, T&& v) {
            using Decayed = std::decay_t<T>;
            if constexpr (std::is_same_v<Decayed, bool>) {
                return JS_NewBool(context, v);
            } else if constexpr (std::is_integral_v<Decayed>) {
                return JS_NewInt32(context, static_cast<int32_t>(v));
            } else if constexpr (std::is_floating_point_v<Decayed>) {
                return JS_NewFloat64(context, static_cast<double>(v));
            } else if constexpr (std::is_convertible_v<Decayed, std::string_view>) {
                const std::string_view sv = v;
                return JS_NewStringLen(context, sv.data(), sv.size());
            } else if constexpr (std::is_same_v<Decayed, Value>) {
                return JS_DupValue(context, v.get());
            } else if constexpr (std::is_class_v<Decayed>) {
                JSRuntime* rt = JS_GetRuntime(context);
                JSClassID class_id = 0;

                // Thread-safe lookup of the Class ID
                {
                    std::shared_lock lock(detail::JSClassInfo<Decayed>::mutex);
                    if (detail::JSClassInfo<Decayed>::ids.contains(rt)) {
                        class_id = detail::JSClassInfo<Decayed>::ids.at(rt);
                    }
                }

                if (class_id != 0) {
                    JSValue obj = JS_NewObjectClass(context, class_id);
                    if (!JS_IsException(obj)) {
                        auto* ptr = new Decayed(std::forward<T>(v));
                        JS_SetOpaque(obj, ptr);
                        return obj;
                    }
                }
                return JS_UNDEFINED;
            } else if constexpr (std::is_integral_v<Decayed> || std::is_enum_v<Decayed>) {
                return JS_NewInt32(context, static_cast<int32_t>(v));
            } else {
                static_assert(std::is_void_v<T>, "Attempted to create JSValue from unsupported type.");
            }
            return JS_UNDEFINED;
        }

        // --- Fluent High-Level API ---

        template <typename T>
        Value& set_variable(const std::string_view name, T&& v) {
            JS_SetPropertyStr(ctx, val, name.data(), create_js_value(ctx, std::forward<T>(v)));
            return *this;
        }

        template <typename T>
        Value& set_constant(const std::string_view name, T&& v) {
            JS_DefinePropertyValueStr(ctx, val, name.data(), create_js_value(ctx, std::forward<T>(v)), JS_PROP_ENUMERABLE);
            return *this;
        }

        Value& set_cfunction(const std::string_view name, JSCFunction* func, const int length = 0) {
            JSValue js_func = JS_NewCFunction(ctx, func, name.data(), length);
            JS_SetPropertyStr(ctx, val, name.data(), js_func);
            return *this;
        }

        template <typename F>
        Value& set_function(const std::string_view name, F&& func) {
            using DecayedF = std::decay_t<F>;
            using Traits = detail::function_traits<DecayedF>;

            auto* func_ptr = new DecayedF(std::forward<F>(func));

            JSValue data_obj = JS_NewArrayBuffer(
                ctx,
                reinterpret_cast<uint8_t*>(func_ptr),
                sizeof(DecayedF),
                detail::lambda_free_func<DecayedF>,
                nullptr,
                false
            );

            // FIX: Safely handle QuickJS allocation failures
            if (JS_IsException(data_obj)) {
                delete func_ptr;
                throw std::runtime_error("qjs::Value error: Failed to allocate ArrayBuffer for closure.");
            }

            JSValue js_func = JS_NewCFunctionData(
                ctx,
                detail::cpp_closure_trampoline<DecayedF>,
                Traits::arity,
                0,
                1,
                &data_obj
            );

            JS_SetPropertyStr(ctx, val, name.data(), js_func);
            JS_FreeValue(ctx, data_obj);

            return *this;
        }

        template <typename T>
        Value& set_element(const uint32_t index, T&& v) {
            if (!is_array()) {
                const JSValue new_val = create_js_value(ctx, std::forward<T>(v));
                JS_FreeValue(ctx, new_val);
                throw std::runtime_error("qjs::Value error: set_element called on a non-array value.");
            }

            int result = JS_SetPropertyUint32(ctx, val, index, create_js_value(ctx, std::forward<T>(v)));
            if (result < 0) {
                throw std::runtime_error("qjs::Value error: QuickJS failed to set the array element.");
            }

            return *this;
        }

        // --- Retrieval & Checking ---

        [[nodiscard]] Value get_property(const std::string_view name) const {
            return {ctx, JS_GetPropertyStr(ctx, val, name.data())};
        }

        [[nodiscard]] bool has_property(const std::string_view name) const {
            const JSAtom atom = JS_NewAtomLen(ctx, name.data(), name.size());
            if (atom == JS_ATOM_NULL) return false;
            const int has_prop = JS_HasProperty(ctx, val, atom);
            JS_FreeAtom(ctx, atom);
            return has_prop == 1;
        }

        [[nodiscard]] Value get_element(const uint32_t index) const {
            return {ctx, JS_GetPropertyUint32(ctx, val, index)};
        }

        template <typename T>
        [[nodiscard]] std::optional<T> as() const {
            using Decayed = std::decay_t<T>;

            if constexpr (std::is_same_v<Decayed, Value>) {
                return *this;
            } else if constexpr (std::is_same_v<Decayed, bool>) {
                if (int v = JS_ToBool(ctx, val); v != -1) return v == 1;
            } else if constexpr (std::is_integral_v<Decayed>) {
                int64_t v;
                if (JS_ToInt64(ctx, &v, val) == 0) return static_cast<T>(v);
            } else if constexpr (std::is_floating_point_v<Decayed>) {
                double v;
                if (JS_ToFloat64(ctx, &v, val) == 0) return static_cast<T>(v);
            } else if constexpr (std::is_same_v<Decayed, std::string>) {
                if (!is_string() && !is_number()) return std::nullopt;
                if (const char* cstr = JS_ToCString(ctx, val)) {
                    std::string str(cstr);
                    JS_FreeCString(ctx, cstr);
                    return str;
                } else {
                    // FIX: Clear context exception to prevent dangling JS errors
                    JS_FreeValue(ctx, JS_GetException(ctx));
                }
            } else if constexpr (std::is_same_v<Decayed, std::string_view>) {
                if (!is_string() && !is_number()) return std::nullopt;
                size_t len;
                if (const char* cstr = JS_ToCStringLen(ctx, &len, val)) {
                    std::string_view sv(cstr, len);
                    return sv;
                } else {
                    // FIX: Clear context exception to prevent dangling JS errors
                    JS_FreeValue(ctx, JS_GetException(ctx));
                }
            } else if constexpr (std::is_class_v<Decayed>) {
                JSRuntime* rt = JS_GetRuntime(ctx);
                JSClassID class_id = 0;

                // Thread-safe read for the Class ID
                {
                    std::shared_lock lock(detail::JSClassInfo<Decayed>::mutex);
                    if (detail::JSClassInfo<Decayed>::ids.contains(rt)) {
                        class_id = detail::JSClassInfo<Decayed>::ids.at(rt);
                    }
                }

                if (class_id != 0) {
                    void* opaque = JS_GetOpaque(val, class_id);
                    if (opaque) {
                        return *static_cast<Decayed*>(opaque);
                    }
                }
                return std::nullopt;
            } else {
                static_assert(std::is_void_v<T>, "Unsupported type requested in as<T>()");
            }
            return std::nullopt;
        }

        // --- Type Checking ---
        [[nodiscard]] bool is_undefined() const { return JS_IsUndefined(val); }
        [[nodiscard]] bool is_null() const      { return JS_IsNull(val); }
        [[nodiscard]] bool is_bool() const      { return JS_IsBool(val); }
        [[nodiscard]] bool is_number() const    { return JS_IsNumber(val); }
        [[nodiscard]] bool is_string() const    { return JS_IsString(val); }
        [[nodiscard]] bool is_object() const    { return JS_IsObject(val); }
        [[nodiscard]] bool is_array() const     { return JS_IsArray(val) == 1; }
        [[nodiscard]] bool is_function() const  { return JS_IsFunction(ctx, val); }
        [[nodiscard]] bool is_error() const     { return JS_IsError(val); }
        [[nodiscard]] bool is_exception() const { return JS_IsException(val); }

        [[nodiscard]] Value call(const std::vector<Value>& args = {}, const Value& this_obj = Value()) const {
            std::vector<JSValue> raw_args;
            raw_args.reserve(args.size());
            for (const auto& arg : args) {
                raw_args.push_back(arg.get());
            }

            const JSValue result = JS_Call(ctx, val, this_obj.get(), static_cast<int>(raw_args.size()), raw_args.data());
            return {ctx, result};
        }

        static Value create_object(JSContext* ctx) {
            return {ctx, JS_NewObject(ctx)};
        }

        static Value create_array(JSContext* ctx) {
            return {ctx, JS_NewArray(ctx)};
        }

        [[nodiscard]] JSValue get() const { return val; }
        [[nodiscard]] JSContext* context() const { return ctx; }

    private:
        JSContext* ctx{nullptr};
        JSValue val{JS_UNDEFINED};
    };

    namespace detail {
        template <typename F, typename Traits, std::size_t... I>
        JSValue cpp_closure_trampoline_impl(JSContext* ctx, F* func, int argc, JSValueConst* argv, std::index_sequence<I...>) {
            try {
                // 1. Extract arguments
                auto args_tuple = std::make_tuple(
                    (I < static_cast<std::size_t>(argc) ?
                        Value(ctx, JS_DupValue(ctx, argv[I])).as<std::tuple_element_t<I, typename Traits::args_tuple>>().value_or(std::tuple_element_t<I, typename Traits::args_tuple>{})
                        : std::tuple_element_t<I, typename Traits::args_tuple>{})...
                );

                // 2. Execute the function
                JSValue result_val;
                if constexpr (std::is_void_v<typename Traits::result_type>) {
                    std::apply(*func, args_tuple);
                    result_val = JS_UNDEFINED;
                } else {
                    auto result = std::apply(*func, args_tuple);
                    result_val = Value::create_js_value(ctx, result);
                }

                // 3. Cleanup: Free any C-strings allocated for std::string_view or const char*
                auto free_if_string = [&](auto& arg) {
                    using ArgType = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<ArgType, std::string_view>) {
                        JS_FreeCString(ctx, arg.data());
                    } else if constexpr (std::is_same_v<ArgType, const char*>) {
                        JS_FreeCString(ctx, arg);
                    }
                };
                std::apply([&](auto&... args) { (free_if_string(args), ...); }, args_tuple);

                return result_val;

            } catch (const std::exception& e) {
                return JS_ThrowInternalError(ctx, "%s", e.what());
            }
        }

        template <typename F>
        JSValue cpp_closure_trampoline(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* func_data) {
            using Traits = function_traits<F>;

            size_t size = 0;
            uint8_t* ptr = JS_GetArrayBuffer(ctx, &size, func_data[0]);
            if (!ptr) {
                return JS_UNDEFINED;
            }

            F* func = reinterpret_cast<F*>(ptr);
            return cpp_closure_trampoline_impl<F, Traits>(ctx, func, argc, argv, std::make_index_sequence<Traits::arity>{});
        }
    }
}


namespace qjs {

    namespace detail {

        // 2. Trampolines mapping JS calls back to C++ pointers
        template <typename T>
        JSValue class_constructor_dispatcher(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
            if (JS_IsUndefined(new_target)) {
                return JS_ThrowTypeError(ctx, "Constructor must be called with 'new'");
            }

            JSRuntime* rt = JS_GetRuntime(ctx);
            std::vector<std::function<T*(JSContext*, int, JSValueConst*)>> ctors;
            JSClassID class_id = 0;

            // FIX: Thread-safe read of registered constructors and class ID
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

        // --- NEW: Structures and Trampolines for Getter/Setter Methods ---

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

            // FIX: Thread-safe push of the constructor mapping
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

            // FIX: Graceful handling of QuickJS memory allocation exception
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

            // FIX: Graceful handling of QuickJS memory allocation exception
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

            // FIX: Graceful handling of QuickJS memory allocation exception
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

            // FIX: Thread-safe lookup
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








 // FIX: Added for thread safety


namespace qjs {

    namespace detail {
        // QuickJS native modules use a C callback for initialization.
        // We map the module definition pointer to our captured C++ lambdas and values.
        struct ModuleRegistry {
            static inline std::mutex mutex; // FIX: Protects global module initialization state
            static inline std::unordered_map<JSModuleDef*, std::vector<std::function<void(JSContext*, JSModuleDef*)>>> inits;

            static int init_trampoline(JSContext* ctx, JSModuleDef* m) {
                std::vector<std::function<void(JSContext*, JSModuleDef*)>> actions;

                // FIX: Safely extract and remove the initialization actions
                {
                    std::unique_lock lock(mutex);
                    auto it = inits.find(m);
                    if (it != inits.end()) {
                        actions = std::move(it->second);
                        inits.erase(it);
                    }
                }

                // Execute all bound exports (values, functions, etc.)
                // This is done outside the lock to prevent deadlocks if an action triggers another JS evaluation.
                for (const auto& action : actions) {
                    action(ctx, m);
                }

                return 0;
            }
        };
    }

    class ModuleBuilder {
    public:
        ModuleBuilder(JSContext* ctx, const std::string_view name) : ctx(ctx), name(name) {}

        ModuleBuilder(const ModuleBuilder&) = delete;
        ModuleBuilder& operator=(const ModuleBuilder&) = delete;

        ModuleBuilder(ModuleBuilder&& other) noexcept
            : ctx(std::exchange(other.ctx, nullptr)),
              name(std::move(other.name)),
              exports(std::move(other.exports)),
              init_actions(std::move(other.init_actions)) {}

        ~ModuleBuilder() {
            if (!ctx) return;

            // 1. Create the native module
            JSModuleDef* m = JS_NewCModule(ctx, name.c_str(), detail::ModuleRegistry::init_trampoline);
            if (!m) return;

            // 2. Transfer our actions into the global static registry
            // FIX: Thread-safe insertion
            {
                std::unique_lock lock(detail::ModuleRegistry::mutex);
                detail::ModuleRegistry::inits[m] = std::move(init_actions);
            }

            // 3. Declare the export names to the engine (Required before init is called)
            for (const auto& exp : exports) {
                JS_AddModuleExport(ctx, m, exp.c_str());
            }
        }

        template <typename T>
        ModuleBuilder& add_value(const std::string_view prop_name, T&& v) {
            std::string name_str(prop_name);
            exports.push_back(name_str);

            using Decayed = std::decay_t<T>;
            init_actions.push_back([name_str, val = Decayed(std::forward<T>(v))](JSContext* c, JSModuleDef* m) mutable {
                JSValue js_val = Value::create_js_value(c, std::move(val));
                JS_SetModuleExport(c, m, name_str.c_str(), js_val);
            });

            return *this;
        }

        template <typename F>
        ModuleBuilder& add_function(const std::string_view func_name, F&& func) {
            std::string name_str(func_name);
            exports.push_back(name_str);

            using DecayedF = std::decay_t<F>;
            using Traits = detail::function_traits<DecayedF>;

            init_actions.push_back([name_str, f = DecayedF(std::forward<F>(func))](JSContext* c, JSModuleDef* m) mutable {
                auto* func_ptr = new DecayedF(std::move(f));

                JSValue data_obj = JS_NewArrayBuffer(
                    c, reinterpret_cast<uint8_t*>(func_ptr), sizeof(DecayedF),
                    detail::lambda_free_func<DecayedF>, nullptr, false
                );

                // FIX: Graceful handling of QuickJS memory allocation exception
                if (JS_IsException(data_obj)) {
                    delete func_ptr;
                    throw std::runtime_error("ModuleBuilder: Failed to allocate ArrayBuffer for function export.");
                }

                JSValue js_func = JS_NewCFunctionData(
                    c, detail::cpp_closure_trampoline<DecayedF>, Traits::arity, 0, 1, &data_obj
                );

                JS_SetModuleExport(c, m, name_str.c_str(), js_func);
                JS_FreeValue(c, data_obj);
            });

            return *this;
        }

    private:
        JSContext* ctx{nullptr};
        std::string name;
        std::vector<std::string> exports;
        std::vector<std::function<void(JSContext*, JSModuleDef*)>> init_actions;
    };

} // namespace qjs

namespace qjs {

    enum class EvalType {
        Global,
        Module
    };

    class Engine {
    public:

        Engine() {
            rt = JS_NewRuntime();
            if (!rt) {
                throw std::runtime_error("QuickJS-ng: Failed to create JSRuntime");
            }

            ctx = JS_NewContext(rt);
            if (!ctx) {
                JS_FreeRuntime(rt);
                throw std::runtime_error("QuickJS-ng: Failed to create JSContext");
            }

            JS_SetModuleLoaderFunc(rt, [](JSContext* c, const char* base_name, const char* name, void* opaque) -> char* {
                size_t len = std::strlen(name);
                char* normalized = static_cast<char*>(js_malloc(c, len + 1));
                if (normalized) {
                    std::memcpy(normalized, name, len + 1);
                }
                return normalized;
            }, nullptr, nullptr);
        }

        ~Engine() {
            if (ctx) JS_FreeContext(ctx);
            if (rt) JS_FreeRuntime(rt);
        }

        // Rule of Five: QuickJS Runtimes/Contexts shouldn't be shallow copied.
        Engine(const Engine&) = delete;
        Engine& operator=(const Engine&) = delete;

        // Allow moving the engine safely
        Engine(Engine&& other) noexcept
            : rt(std::exchange(other.rt, nullptr)),
              ctx(std::exchange(other.ctx, nullptr)) {}

        Engine& operator=(Engine&& other) noexcept {
            if (this != &other) {
                if (ctx) JS_FreeContext(ctx);
                if (rt) JS_FreeRuntime(rt);
                rt = std::exchange(other.rt, nullptr);
                ctx = std::exchange(other.ctx, nullptr);
            }
            return *this;
        }

        // --- High-Level API ---

        /**
         * Evaluates a string of JavaScript code.
         */
        std::expected<std::string, std::string> eval(const std::string_view code, const std::string_view filename = "<eval>", EvalType eval_type = EvalType::Global) {
            int flags = (eval_type == EvalType::Module) ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL;
            const JSValue val = JS_Eval(ctx, code.data(), code.size(), filename.data(), flags);
            return processResult(val);
        }

        /**
         * Reads a file and evaluates its contents.
         */
        std::expected<std::string, std::string> eval_file(std::string_view filepath, EvalType eval_type = EvalType::Global) {
            std::ifstream file(filepath.data(), std::ios::in | std::ios::binary);
            if (!file) {
                return std::unexpected(std::format("Failed to open file: {}", filepath));
            }
            const std::string code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            return eval(code, filepath, eval_type);
        }

        [[nodiscard]]
        Value global() const {
            return {ctx, JS_GetGlobalObject(ctx)};
        }

        // --- Value Factories --

        Value make_value(const int32_t v) const {
            return Value(ctx, v);
        }

        Value make_value(const double v) const {
            return Value(ctx, v);
        }

        Value make_value(const bool v) const {
            return Value(ctx, v);
        }

        Value make_value(const std::string_view v) const {
            return Value(ctx, v);
        }

        Value make_value(const char* v) const {
            return Value(ctx, v);
        }

        Value make_object() const {
            return Value::create_object(ctx);
        }

        Value make_array() const {
            return Value::create_array(ctx);
        }

        ModuleBuilder make_module(const std::string_view name) const {
            return ModuleBuilder(ctx, name);
        }

        template <typename T>
        [[nodiscard]] ClassBuilder<T> make_class(const std::string_view name) {
            JSClassID active_id = 0;
            const char* class_name_ptr = nullptr;

            // FIX: Thread-safe write/allocation of the class ID
            {
                std::unique_lock lock(detail::JSClassInfo<T>::mutex);
                if (detail::JSClassInfo<T>::ids.find(rt) == detail::JSClassInfo<T>::ids.end()) {
                    JSClassID id = 0;
                    JS_NewClassID(rt, &id);
                    detail::JSClassInfo<T>::ids[rt] = id;
                    detail::JSClassInfo<T>::name = name;
                }
                active_id = detail::JSClassInfo<T>::ids[rt];
                class_name_ptr = detail::JSClassInfo<T>::name.c_str();
            }

            // Register the class inside this specific runtime only once
            if (registered_classes.find(active_id) == registered_classes.end()) {
                JSClassDef def{};
                def.class_name = class_name_ptr;

                // Set up the finalizer to automatically delete the mapped C++ object
                def.finalizer = [](JSRuntime* runtime_ptr, JSValue val) {
                    JSClassID class_id = 0;
                    // FIX: Thread-safe read during GC
                    {
                        std::shared_lock lock(detail::JSClassInfo<T>::mutex);
                        if (detail::JSClassInfo<T>::ids.contains(runtime_ptr)) {
                            class_id = detail::JSClassInfo<T>::ids.at(runtime_ptr);
                        }
                    }

                    if (class_id != 0) {
                        T* ptr = static_cast<T*>(JS_GetOpaque(val, class_id));
                        delete ptr; // Safely release C++ memory
                    }
                };

                JS_NewClass(rt, active_id, &def);
                registered_classes.insert(active_id);

                // FIX: Thread-safe clear
                {
                    std::unique_lock lock(detail::JSClassInfo<T>::mutex);
                    detail::JSClassInfo<T>::constructors[rt].clear();
                }
            }

            return ClassBuilder<T>(ctx, name);
        }

        std::expected<std::vector<uint8_t>, std::string> compile_to_bytecode(const std::string_view code, const std::string_view filename = "<eval>", EvalType eval_type = EvalType::Global) const {
            // Set up our evaluation flags, making sure to include the COMPILE_ONLY flag
            int flags = (eval_type == EvalType::Module) ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL;
            flags |= JS_EVAL_FLAG_COMPILE_ONLY;

            // Ask QuickJS to compile the code into a function object
            JSValue obj = JS_Eval(ctx, code.data(), code.size(), filename.data(), flags);

            // Check if a syntax error or other exception occurred during compilation
            if (JS_IsException(obj)) {
                JSValue exception_val = JS_GetException(ctx);

                // Extract the error message and stack trace
                const char* cstr = JS_ToCString(ctx, exception_val);
                std::string err_msg = cstr ? cstr : "Unknown compilation error";
                if (cstr) JS_FreeCString(ctx, cstr);

                if (JS_IsError(exception_val)) {
                    JSValue stack = JS_GetPropertyStr(ctx, exception_val, "stack");
                    if (!JS_IsUndefined(stack)) {
                        const char* stack_cstr = JS_ToCString(ctx, stack);
                        if (stack_cstr) {
                            err_msg += "\n" + std::string(stack_cstr);
                            JS_FreeCString(ctx, stack_cstr);
                        }
                    }
                    JS_FreeValue(ctx, stack);
                }

                JS_FreeValue(ctx, exception_val);
                JS_FreeValue(ctx, obj);
                return std::unexpected(err_msg);
            }

            // Serialize the function object into bytecode
            size_t out_buf_len = 0;
            // JS_WRITE_OBJ_BYTECODE tells QuickJS to output executable bytecode
            uint8_t* out_buf = JS_WriteObject(ctx, &out_buf_len, obj, JS_WRITE_OBJ_BYTECODE);

            // We no longer need the compiled function object, free it
            JS_FreeValue(ctx, obj);

            if (!out_buf) {
                return std::unexpected("Failed to generate bytecode: JS_WriteObject returned null");
            }

            // Copy the bytecode from the QuickJS C-buffer into a modern C++ vector
            std::vector<uint8_t> bytecode(out_buf, out_buf + out_buf_len);

            // Free the C-buffer allocated by QuickJS
            js_free(ctx, out_buf);

            return bytecode;
        }

        std::expected<std::vector<uint8_t>, std::string> compile_file_to_bytecode(std::string_view filepath, EvalType eval_type = EvalType::Global) const {
            // 1. Open the file in binary read mode
            std::ifstream file(filepath.data(), std::ios::in | std::ios::binary);
            if (!file) {
                return std::unexpected(std::format("Failed to open file: {}", filepath));
            }

            // 2. Read the entire file content into a std::string
            const std::string code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            // 3. Pass the code and the filepath to our core compilation function
            return compile_to_bytecode(code, filepath, eval_type);
        }

        /**
         * Loads and evaluates compiled QuickJS bytecode from memory.
         * If the bytecode is a module, this automatically registers it, making it
         * available for other scripts to import via the filename used during compilation.
         * * @param bytecode The vector of raw bytes.
         * @return The evaluation result as a string, or an error message.
         */
        std::expected<std::string, std::string> eval_bytecode(const std::vector<uint8_t>& bytecode) const {
            // 1. Deserialize the bytecode into a QuickJS object
            // JS_READ_OBJ_BYTECODE tells the engine we are reading executable bytecode
            JSValue obj = JS_ReadObject(ctx, bytecode.data(), bytecode.size(), JS_READ_OBJ_BYTECODE);

            // If the bytecode is invalid or corrupt, extract the error and exit
            if (JS_IsException(obj)) {
                return processResult(obj);
            }

            // 2. Evaluate the reconstructed function/module
            // Note: JS_EvalFunction automatically consumes (frees) the 'obj' parameter,
            // so we don't need to call JS_FreeValue(ctx, obj) ourselves!
            JSValue val = JS_EvalFunction(ctx, obj);

            // 3. Return the result safely using your existing helper
            return processResult(val);
        }

        /**
         * Reads a bytecode file from disk and evaluates it.
         * @param filepath The path to the binary (.bin) file.
         */
        std::expected<std::string, std::string> eval_bytecode_file(std::string_view filepath) const {
            // Open in binary mode
            std::ifstream file(filepath.data(), std::ios::in | std::ios::binary);
            if (!file) {
                return std::unexpected(std::format("Failed to open bytecode file: {}", filepath));
            }

            // Efficiently read the entire binary file into a vector
            file.seekg(0, std::ios::end);
            size_t size = file.tellg();
            file.seekg(0, std::ios::beg);

            std::vector<uint8_t> bytecode(size);
            if (size > 0) {
                file.read(reinterpret_cast<char*>(bytecode.data()), size);
            }

            // Pass the loaded bytes to our core bytecode evaluator
            return eval_bytecode(bytecode);
        }

        void run_gc() const {
            JS_RunGC(rt);
        }

    private:
        JSRuntime* rt{nullptr};
        JSContext* ctx{nullptr};

        std::unordered_set<JSClassID> registered_classes;

        // Helper to extract the result or error, and safely free the JSValues
        std::expected<std::string, std::string> processResult(const JSValue val) const {
            auto toStdString = [this](const JSValue v) -> std::string {
                const char* cstr = JS_ToCString(ctx, v);
                if (!cstr) return "";
                std::string str(cstr);
                JS_FreeCString(ctx, cstr);
                return str;
            };

            if (JS_IsException(val)) {
                const JSValue exception_val = JS_GetException(ctx);
                std::string err_msg = toStdString(exception_val);

                // Check if it's a JS Error object to extract the stack trace
                if (JS_IsError(exception_val)) {
                    const JSValue stack = JS_GetPropertyStr(ctx, exception_val, "stack");
                    if (!JS_IsUndefined(stack)) {
                        err_msg += "\n" + toStdString(stack);
                    }
                    JS_FreeValue(ctx, stack);
                }

                JS_FreeValue(ctx, exception_val);
                JS_FreeValue(ctx, val);
                return std::unexpected(err_msg);
            }

            // Success state
            std::string result = toStdString(val);
            JS_FreeValue(ctx, val);
            return result;
        }
    };

} // namespace qjs