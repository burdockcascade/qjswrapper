/* Auto Generated */
/* Amalgamated Header */
#pragma once

// System Includes
#include <concepts>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <quickjs.h>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>







namespace qjs {

    class Value {
    public:
        // Constructors
        Value(JSContext* ctx, JSValue v) : ctx_(ctx), v_(v) {}

        // Disable default constructor to ensure context is always present
        Value() = delete;

        // Destructor handles memory cleanup
        ~Value() {
            if (ctx_) {
                JS_FreeValue(ctx_, v_);
            }
        }

        // Copy semantics: Increment reference count
        Value(const Value& other) : ctx_(other.ctx_), v_(JS_DupValue(other.ctx_, other.v_)) {}

        Value& operator=(const Value& other) {
            if (this != &other) {
                if (ctx_) JS_FreeValue(ctx_, v_);
                ctx_ = other.ctx_;
                v_ = JS_DupValue(other.ctx_, other.v_);
            }
            return *this;
        }

        // Move semantics: Transfer ownership without changing ref count
        Value(Value&& other) noexcept : ctx_(other.ctx_), v_(other.v_) {
            other.ctx_ = nullptr;
            other.v_ = JS_UNDEFINED;
        }

        Value& operator=(Value&& other) noexcept {
            if (this != &other) {
                if (ctx_) JS_FreeValue(ctx_, v_);
                ctx_ = other.ctx_;
                v_ = other.v_;
                other.ctx_ = nullptr;
                other.v_ = JS_UNDEFINED;
            }
            return *this;
        }

        [[nodiscard]] bool is_object() const { return JS_IsObject(v_); }
        [[nodiscard]] bool is_exception() const { return JS_IsException(v_); }

        // Accessors
        [[nodiscard]] JSValue get() const { return v_; }
        [[nodiscard]] JSContext* ctx() const { return ctx_; }

        // Implicit conversion for compatibility with QuickJS C functions
        operator JSValue() const { return v_; }

    private:
        JSContext* ctx_;
        JSValue v_;
    };

    template<typename T>
    struct converter {
        static T get(JSContext* ctx, JSValueConst v) {
            if constexpr (std::is_same_v<T, bool>) {
                // Explicitly check for truthiness
                return JS_ToBool(ctx, v) > 0;
            } else if constexpr (std::floating_point<T>) {
                // Check floating point before general integrals
                double val = 0;
                JS_ToFloat64(ctx, &val, v);
                return static_cast<T>(val);
            } else if constexpr (std::integral<T>) {
                // Use 64-bit to prevent truncation during extraction
                int64_t val = 0;
                JS_ToInt64(ctx, &val, v);
                return static_cast<T>(val);
            } else if constexpr (std::is_convertible_v<T, std::string>) {
                size_t len;
                const char* str = JS_ToCStringLen(ctx, &len, v);
                std::string s(str ? str : "", len);
                JS_FreeCString(ctx, str);
                return s;
            }
            return T{};
        }

        static Value put(JSContext* ctx, const T& val) {
            if constexpr (std::is_same_v<T, bool>) {
                return { ctx, JS_NewBool(ctx, val) };
            } else if constexpr (std::floating_point<T>) {
                return { ctx, JS_NewFloat64(ctx, static_cast<double>(val)) };
            } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
                return { ctx, JS_NewInt64(ctx, static_cast<int64_t>(val)) };
            } else if constexpr (std::is_convertible_v<T, std::string_view>) {
                return { ctx, JS_NewStringLen(ctx, val.data(), val.size()) };
            }
            return { ctx, JS_UNDEFINED };
        }
    };

    template<>
    struct converter<const char*> {
        static const char* get(JSContext* ctx, JSValueConst v) {
            return JS_ToCString(ctx, v);
        }

        static Value put(JSContext* ctx, const char* val) {
            return { ctx, JS_NewString(ctx, val) };
        }
    };

} // namespace qjs


namespace qjs {

    // --- Function Traits for Type Deduction ---
    template<typename T>
    struct function_traits : function_traits<decltype(&T::operator())> {};

    template<typename R, typename... Args>
    struct function_traits<R(*)(Args...)> {
        using return_type = R;
        using args_tuple = std::tuple<Args...>;
        static constexpr size_t arity = sizeof...(Args);
    };

    template<typename C, typename R, typename... Args>
    struct function_traits<R(C::*)(Args...) const> {
        using return_type = R;
        using args_tuple = std::tuple<Args...>;
        static constexpr size_t arity = sizeof...(Args);
    };

    template<typename C, typename R, typename... Args>
    struct function_traits<R(C::*)(Args...)> {
        using return_type = R;
        using args_tuple = std::tuple<Args...>;
        static constexpr size_t arity = sizeof...(Args);
    };

    // --- Concepts ---
    template<typename T>
    concept callable = (std::is_class_v<std::remove_cvref_t<T>> && requires {
        &std::remove_cvref_t<T>::operator();
    }) || std::is_function_v<std::remove_pointer_t<std::remove_cvref_t<T>>>;

    struct CallableBase {
        virtual ~CallableBase() = default;
    };

    // Strongly-typed wrapper holding the exact functor (avoids std::function overhead)
    template<typename F>
    struct CallableWrapper : CallableBase {
        F func;
        explicit CallableWrapper(F f) : func(std::move(f)) {}
    };

    // --- Bridge Components ---
    inline JSClassID wrapper_class_id = 0;

    template<typename F, typename R, typename ArgsTuple>
    struct Invoker;

    template<typename F, typename R, typename... Args>
    struct Invoker<F, R, std::tuple<Args...>> {
        static JSValue apply(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv, int /*magic*/, JSValue* data) {
            void* p = JS_GetOpaque(data[0], wrapper_class_id);
            if (!p) {
                return JS_ThrowTypeError(ctx, "Failed to retrieve C++ lambda closure");
            }

            auto* wrapper = static_cast<CallableWrapper<F>*>(p);
            auto& func = wrapper->func;

            auto args = [&]<size_t... Is>(std::index_sequence<Is...>) {
                return std::make_tuple(converter<std::decay_t<Args>>::get(ctx, (Is < argc ? argv[Is] : JS_UNDEFINED))...);
            }(std::index_sequence_for<Args...>{});

            try {
                if constexpr (std::is_void_v<R>) {
                    std::apply(func, std::move(args));
                    return JS_UNDEFINED;
                } else {
                    Value ret = converter<R>::put(ctx, std::apply(func, std::move(args)));
                    return JS_DupValue(ctx, ret.get());
                }
            } catch (const std::exception& e) {
                return JS_ThrowInternalError(ctx, "%s", e.what());
            } catch (...) {
                return JS_ThrowInternalError(ctx, "Unknown C++ exception occurred");
            }
        }
    };

    // A helper to track Class IDs for C++ types
    template<typename T>
    struct ClassRegistry {
        static inline JSClassID class_id = 0;
        static inline std::string name;

        static void init(JSRuntime* rt, const std::string& class_name) {
            if (class_id == 0) {
                JS_NewClassID(rt, &class_id);
                name = class_name;
            }

            if (!JS_IsRegisteredClass(rt, class_id)) {
                JSClassDef def{
                    .class_name = name.c_str(),
                    .finalizer = [](JSRuntime* /*rt*/, JSValue val) {
                        auto* obj = static_cast<T*>(JS_GetOpaque(val, class_id));
                        delete obj; // JS GC calls C++ destructor
                    }
                };
                JS_NewClass(rt, class_id, &def);
            }
        }
    };

    template<typename MemFunc>
    struct MemberCallableWrapper : CallableBase {
        MemFunc func;
        explicit MemberCallableWrapper(MemFunc f) : func(f) {}
    };

    inline void init_wrapper_class(JSContext* ctx) {
        auto rt = JS_GetRuntime(ctx);
        if (wrapper_class_id == 0) {
            JS_NewClassID(rt, &wrapper_class_id);
        }
        if (!JS_IsRegisteredClass(rt, wrapper_class_id)) {
            JSClassDef def{
                .class_name = "CppLambda",
                .finalizer = [](JSRuntime* /*rt*/, JSValue val) {
                    auto* base = static_cast<CallableBase*>(JS_GetOpaque(val, wrapper_class_id));
                    delete base;
                }
            };
            JS_NewClass(rt, wrapper_class_id, &def);
        }
    }

    template<typename MethodType, typename C, typename R, typename... Args>
    struct MemberInvokerImpl {
        static JSValue apply(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int /*magic*/, JSValue* data) {
            C* instance = static_cast<C*>(JS_GetOpaque(this_val, ClassRegistry<C>::class_id));
            if (!instance) return JS_ThrowTypeError(ctx, "Method called on incompatible object");

            void* p = JS_GetOpaque(data[0], wrapper_class_id);
            if (!p) return JS_ThrowTypeError(ctx, "Failed to retrieve member function pointer");

            auto* wrapper = static_cast<MemberCallableWrapper<MethodType>*>(p);
            MethodType method_ptr = wrapper->func;

            auto args = [&]<size_t... Is>(std::index_sequence<Is...>) {
                return std::make_tuple(converter<std::decay_t<Args>>::get(ctx, (Is < argc ? argv[Is] : JS_UNDEFINED))...);
            }(std::index_sequence_for<Args...>{});

            try {
                auto caller = [instance, method_ptr](auto&&... unpacked) {
                    return (instance->*method_ptr)(std::forward<decltype(unpacked)>(unpacked)...);
                };

                if constexpr (std::is_void_v<R>) {
                    std::apply(caller, std::move(args));
                    return JS_UNDEFINED;
                } else {
                    Value ret = converter<R>::put(ctx, std::apply(caller, std::move(args)));
                    return JS_DupValue(ctx, ret.get());
                }
            } catch (const std::exception& e) {
                return JS_ThrowInternalError(ctx, "%s", e.what());
            } catch (...) {
                return JS_ThrowInternalError(ctx, "Unknown exception in member function");
            }
        }
    };

    template<typename F>
    requires callable<F>
    JSValue create_js_function(JSContext* ctx, F&& func) {
        using traits = function_traits<std::decay_t<F>>;
        using R = typename traits::return_type;
        using ArgsTuple = typename traits::args_tuple;
        using DecayF = std::decay_t<F>; // Deduce the raw functor

        init_wrapper_class(ctx);

        JSValue opaque_obj = JS_NewObjectClass(ctx, wrapper_class_id);
        auto* func_ptr = new CallableWrapper<DecayF>(std::forward<F>(func));
        JS_SetOpaque(opaque_obj, func_ptr);

        JSValue js_func = JS_NewCFunctionData(ctx,
            &Invoker<DecayF, R, ArgsTuple>::apply,
            static_cast<int>(traits::arity), 0, 1, &opaque_obj
        );

        JS_FreeValue(ctx, opaque_obj);
        return js_func;
    }

    template<typename C, typename R, typename... Args>
    JSValue create_js_function(JSContext* ctx, R (C::*method)(Args...)) {
        init_wrapper_class(ctx);
        using MemFunc = R (C::*)(Args...);

        JSValue opaque_obj = JS_NewObjectClass(ctx, wrapper_class_id);
        auto* func_ptr = new MemberCallableWrapper<MemFunc>(method);
        JS_SetOpaque(opaque_obj, func_ptr);

        JSValue js_func = JS_NewCFunctionData(ctx,
            &MemberInvokerImpl<MemFunc, C, R, Args...>::apply,
            sizeof...(Args), 0, 1, &opaque_obj
        );

        JS_FreeValue(ctx, opaque_obj);
        return js_func;
    }

    template<typename C, typename R, typename... Args>
    JSValue create_js_function(JSContext* ctx, R (C::*method)(Args...) const) {
        init_wrapper_class(ctx);
        using MemFunc = R (C::*)(Args...) const;

        JSValue opaque_obj = JS_NewObjectClass(ctx, wrapper_class_id);
        auto* func_ptr = new MemberCallableWrapper<MemFunc>(method);
        JS_SetOpaque(opaque_obj, func_ptr);

        JSValue js_func = JS_NewCFunctionData(ctx,
            &MemberInvokerImpl<MemFunc, C, R, Args...>::apply,
            sizeof...(Args), 0, 1, &opaque_obj
        );

        JS_FreeValue(ctx, opaque_obj);
        return js_func;
    }

    // --- Constructor Dispatcher ---
    // Manages multiple constructor overloads for a specific class T
    template<typename T>
    struct ConstructorDispatcher : CallableBase {
        using Factory = std::function<T*(JSContext*, int, JSValueConst*)>;

        struct Overload {
            size_t arity;
            Factory factory;
        };
        std::vector<Overload> overloads;

        static JSValue apply(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv, int /*magic*/, JSValue* data) {
            void* p = JS_GetOpaque(data[0], wrapper_class_id);
            if (!p) return JS_ThrowTypeError(ctx, "Failed to retrieve constructor dispatcher");

            auto* dispatcher = static_cast<ConstructorDispatcher<T>*>(p);

            for (const auto& overload : dispatcher->overloads) {
                if (static_cast<size_t>(argc) == overload.arity) {
                    try {
                        T* instance = overload.factory(ctx, argc, argv);
                        if (instance) {
                            JSValue obj = JS_NewObjectClass(ctx, ClassRegistry<T>::class_id);
                            if (JS_IsException(obj)) {
                                delete instance;
                                return obj;
                            }
                            JS_SetOpaque(obj, instance);
                            return obj;
                        }
                    } catch (const std::exception& e) {
                        return JS_ThrowInternalError(ctx, "Constructor error: %s", e.what());
                    } catch (...) {
                        return JS_ThrowInternalError(ctx, "Unknown exception in constructor");
                    }
                }
            }
            return JS_ThrowTypeError(ctx, "No matching constructor found for given argument count");
        }
    };

} // namespace qjs

namespace qjs {

    enum class Prop {
        Normal,           // Writable | Enumerable | Configurable
        ReadOnly,         // Enumerable | Configurable
        Hidden,           // Writable | Configurable
        HiddenReadOnly,   // Configurable
        Locked,           // Enumerable (Not writable, not configurable)
        Internal          // None (Fully locked down and hidden)
    };

    class Object {
    public:
        explicit Object(Value v) : val_(std::move(v)) {}

        template<typename T>
        requires (!callable<T> && !std::is_member_function_pointer_v<std::decay_t<T>>)
        Object& set(std::string_view prop, T&& value, const Prop mode) {
            auto ctx = val_.ctx();
            const Value v = converter<std::decay_t<T>>::put(ctx, std::forward<T>(value));
            const int flags = resolve_flags(mode);
            JS_DefinePropertyValueStr(ctx, val_.get(), prop.data(), JS_DupValue(ctx, v.get()), flags);
            return *this;
        }

        template<typename T>
        requires (!callable<T> && !std::is_member_function_pointer_v<std::decay_t<T>>)
        Object& set_variable(std::string_view prop, T&& value) {
            return set(prop, std::forward<T>(value), Prop::Normal);
        }

        template<typename T>
        requires (!callable<T> && !std::is_member_function_pointer_v<std::decay_t<T>>)
        Object& set_constant(std::string_view prop, T&& value) {
            return set(prop, std::forward<T>(value), Prop::Locked);
        }

        template<typename F>
        requires (callable<F> || std::is_member_function_pointer_v<std::decay_t<F>>)
        Object& set(const std::string_view prop, F&& func, const Prop mode) {
            const auto ctx = val_.ctx();
            const JSValue js_func = create_js_function(ctx, std::forward<F>(func));
            const int flags = resolve_flags(mode);
            JS_DefinePropertyValueStr(ctx, val_.get(), prop.data(), js_func, flags);
            return *this;
        }

        template<typename F>
        requires (callable<F> || std::is_member_function_pointer_v<std::decay_t<F>>)
        Object& set_function(const std::string_view prop, F&& func) {
            return set(prop, std::forward<F>(func), Prop::ReadOnly);
        }

        template<typename T>
        T get(const std::string_view prop) const {
            auto ctx = val_.ctx();
            JSValue p = JS_GetPropertyStr(ctx, val_.get(), prop.data());
            T result = converter<T>::get(ctx, p);
            JS_FreeValue(ctx, p);
            return result;
        }

        [[nodiscard]] bool remove(const std::string_view prop) const {
            const auto ctx = val_.ctx();
            const JSAtom atom = JS_NewAtomLen(ctx, prop.data(), prop.size());
            const int ret = JS_DeleteProperty(ctx, val_.get(), atom, 0);
            JS_FreeAtom(ctx, atom);
            return ret == 1;
        }

        template<typename... Args>
        Value invoke(const std::string_view prop, Args&&... args) const {
            auto ctx = val_.ctx();
            const JSValue func = JS_GetPropertyStr(ctx, val_.get(), prop.data());

            if (!JS_IsFunction(ctx, func)) {
                JS_FreeValue(ctx, func);
                return { ctx, JS_ThrowTypeError(ctx, "Property '%s' is not a function", prop.data()) };
            }

            std::vector<Value> managed_args;
            managed_args.reserve(sizeof...(Args));
            (managed_args.push_back(converter<std::decay_t<Args>>::put(ctx, std::forward<Args>(args))), ...);

            std::vector<JSValue> raw_args;
            raw_args.reserve(sizeof...(Args));
            for (const auto& arg : managed_args) raw_args.push_back(arg.get());

            JSValue result = JS_Call(ctx, func, val_.get(), raw_args.size(), raw_args.data());
            JS_FreeValue(ctx, func);

            return { ctx, result };
        }

        [[nodiscard]] std::vector<std::string> keys() const {
            const auto ctx = val_.ctx();
            JSPropertyEnum *ptab = nullptr;
            uint32_t plen = 0;
            std::vector<std::string> result;

            if (JS_GetOwnPropertyNames(ctx, &ptab, &plen, val_.get(), JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
                result.reserve(plen);
                for(uint32_t i = 0; i < plen; i++) {
                    if (const char* str = JS_AtomToCString(ctx, ptab[i].atom)) {
                        result.emplace_back(str);
                        JS_FreeCString(ctx, str);
                    }
                    JS_FreeAtom(ctx, ptab[i].atom);
                }
                js_free(ctx, ptab);
            }
            return result;
        }

        [[nodiscard]] bool has(const std::string_view prop) const {
            const JSAtom atom = JS_NewAtomLen(val_.ctx(), prop.data(), prop.size());
            const int ret = JS_HasProperty(val_.ctx(), val_.get(), atom);
            JS_FreeAtom(val_.ctx(), atom);
            return ret > 0;
        }

        [[nodiscard]] const Value& as_value() const { return val_; }

    private:
        Value val_;

        static int resolve_flags(const Prop mode) {
            switch (mode) {
                case Prop::Normal:         return JS_PROP_WRITABLE | JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE;
                case Prop::ReadOnly:       return JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE;
                case Prop::Hidden:         return JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE;
                case Prop::HiddenReadOnly: return JS_PROP_CONFIGURABLE;
                case Prop::Locked:         return JS_PROP_ENUMERABLE;
                case Prop::Internal:       return 0; // No flags
                default:                   return JS_PROP_WRITABLE | JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE;
            }
        }
    };

    template<>
    struct converter<Object> {
        static Value put(JSContext* ctx, const Object& val) {
            return val.as_value();
        }

        static Object get(JSContext* ctx, JSValueConst v) {
            return Object(Value(ctx, JS_DupValue(ctx, v)));
        }
    };

} // namespace qjs



namespace qjs {

    template<typename T>
    class Class {
    public:
        Class(Object constructor, Object prototype, ConstructorDispatcher<T>* dispatcher)
            : constructor_(std::move(constructor)), prototype_(std::move(prototype)), dispatcher_(dispatcher) {}

        template<typename... Args>
        Class& constructor() {
            auto factory = [](JSContext* ctx, int argc, JSValueConst* argv) -> T* {
                auto args = [&]<size_t... Is>(std::index_sequence<Is...>) {
                    return std::make_tuple(converter<std::decay_t<Args>>::get(ctx, (Is < argc ? argv[Is] : JS_UNDEFINED))...);
                }(std::index_sequence_for<Args...>{});

                return std::apply([](auto&&... unpacked) {
                    return new T(std::forward<decltype(unpacked)>(unpacked)...);
                }, std::move(args));
            };

            dispatcher_->overloads.push_back({sizeof...(Args), factory});
            return *this;
        }

        template<typename F>
        requires callable<F>
        Class& constructor(F&& func) {
            using traits = function_traits<std::decay_t<F>>;
            register_custom_factory(std::forward<F>(func), typename traits::args_tuple{});
            return *this;
        }

        template<typename F>
        requires (callable<F> || std::is_member_function_pointer_v<std::decay_t<F>>)
        Class& method(std::string_view name, F&& func) {
            prototype_.set_function(name, std::forward<F>(func));
            return *this;
        }

        template<typename F>
        requires callable<F>
        Class& static_method(std::string_view name, F&& func) {
            constructor_.set_function(name, std::forward<F>(func));
            return *this;
        }

        template<typename V>
        requires (!callable<V> && !std::is_member_function_pointer_v<std::decay_t<V>>)
        Class& variable(std::string_view name, V&& value) {
            prototype_.set_variable(name, std::forward<V>(value));
            return *this;
        }

        template<typename V>
        requires (!callable<V> && !std::is_member_function_pointer_v<std::decay_t<V>>)
        Class& constant(std::string_view name, V&& value) {
            prototype_.set_constant(name, std::forward<V>(value));
            return *this;
        }

        template<typename V>
        requires (!callable<V> && !std::is_member_function_pointer_v<std::decay_t<V>>)
        Class& static_variable(std::string_view name, V&& value) {
            constructor_.set_variable(name, std::forward<V>(value));
            return *this;
        }

        template<typename V>
        requires (!callable<V> && !std::is_member_function_pointer_v<std::decay_t<V>>)
        Class& static_constant(std::string_view name, V&& value) {
            constructor_.set_constant(name, std::forward<V>(value));
            return *this;
        }

        [[nodiscard]] const Object& get_constructor() const {
            return constructor_;
        }

    private:
        Object constructor_;
        Object prototype_;
        ConstructorDispatcher<T>* dispatcher_;

        template<typename F, typename... Args>
        void register_custom_factory(F func, std::tuple<Args...>) {
            using traits = function_traits<std::decay_t<F>>;
            using Ret = typename traits::return_type;
            constexpr size_t arity = traits::arity;

            auto factory = [f = std::move(func)](JSContext* ctx, int argc, JSValueConst* argv) -> T* {
                auto args = [&]<size_t... Is>(std::index_sequence<Is...>) {
                    return std::make_tuple(converter<std::decay_t<Args>>::get(ctx, (Is < argc ? argv[Is] : JS_UNDEFINED))...);
                }(std::index_sequence_for<Args...>{});

                if constexpr (std::is_pointer_v<Ret> && std::is_same_v<std::remove_pointer_t<Ret>, T>) {
                    return std::apply(f, std::move(args));
                } else if constexpr (std::is_same_v<Ret, T>) {
                    return new T(std::apply(f, std::move(args)));
                } else {
                    static_assert(std::is_same_v<Ret, T> || (std::is_pointer_v<Ret> && std::is_same_v<std::remove_pointer_t<Ret>, T>),
                                  "Custom constructor must return T or T*");
                    return nullptr;
                }
            };

            dispatcher_->overloads.push_back({arity, factory});
        }
    };

} // namespace qjs






namespace qjs {

    class Module {
    public:
        Module(JSContext* ctx, JSModuleDef* m, std::function<void(std::string, Value)> add_cb)
            : ctx_(ctx), m_(m), add_cb_(std::move(add_cb)) {}

        template<typename T>
        requires (!callable<T> && !std::is_member_function_pointer_v<std::decay_t<T>>)
        Module& add(const std::string& name, T&& value) {
            JS_AddModuleExport(ctx_, m_, name.c_str());
            add_cb_(name, converter<std::decay_t<T>>::put(ctx_, std::forward<T>(value)));
            return *this;
        }

        template<typename F>
        requires (callable<F> || std::is_member_function_pointer_v<std::decay_t<F>>)
        Module& add(const std::string& name, F&& func) {
            JS_AddModuleExport(ctx_, m_, name.c_str());
            const JSValue js_func = create_js_function(ctx_, std::forward<F>(func));
            add_cb_(name, Value(ctx_, js_func));
            return *this;
        }

        template<typename T>
        Module& add(const std::string& name, Class<T>& cls) {
            return add(name, cls.get_constructor());
        }

        template<typename T>
        Module& add(const std::string& name, const Class<T>& cls) {
            return add(name, cls.get_constructor());
        }

    private:
        JSContext* ctx_;
        JSModuleDef* m_;
        std::function<void(std::string, Value)> add_cb_;
    };

} // namespace qjs

namespace qjs {

    enum class EvalMode {
        Script, // Runs in the global context (variables become global)
        Module  // Runs as an ES6 module (supports import/export, strict mode by default)
    };

    struct EngineError {
        std::string message;
        std::string filename;
        int line_number = -1;
        std::string stack;

        [[nodiscard]] std::string to_string() const {
            std::string out = message;
            if (!stack.empty()) {
                out += "\nStack:\n" + stack;
            }
            return out;
        }
    };

    class Engine {

    public:
        Engine() : rt(JS_NewRuntime()), ctx(JS_NewContext(rt.get())), global_wrapper(Value(ctx.get(), JS_GetGlobalObject(ctx.get()))) {
            JS_SetContextOpaque(ctx.get(), this);

            JS_SetHostPromiseRejectionTracker(rt.get(), promise_rejection_tracker, this);

            JS_SetModuleLoaderFunc(rt.get(), nullptr, [](JSContext* ctx, const char* module_name, void* opaque) -> JSModuleDef* {
                Engine* eng = static_cast<Engine*>(opaque);
                std::string name_str(module_name);

                // 1. Check Native C++ modules
                for (const auto& mod : eng->modules_) {
                    if (mod->name == name_str) return mod->js_module;
                }

                // 2. Check pre-registered JS Source modules
                auto s_it = eng->source_modules_.find(name_str);
                if (s_it != eng->source_modules_.end()) {
                    JSValue func_val = JS_Eval(ctx, s_it->second.c_str(), s_it->second.size(),
                                               module_name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
                    if (JS_IsException(func_val)) return nullptr;
                    auto* m = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(func_val));
                    JS_FreeValue(ctx, func_val);
                    return m;
                }

                // 3. Check Pre-registered Embedded Bytecode modules
                auto b_it = eng->bytecode_modules_.find(name_str);
                if (b_it != eng->bytecode_modules_.end()) {
                    JSValue obj = JS_ReadObject(ctx, b_it->second.data, b_it->second.len, JS_READ_OBJ_BYTECODE);
                    if (JS_IsException(obj)) return nullptr;
                    auto* m = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(obj));
                    JS_FreeValue(ctx, obj);
                    return m;
                }

                // 4. Fallback: Try loading as a standard JS file from the filesystem
                std::filesystem::path p(name_str);
                if (!std::filesystem::exists(p)) {
                    JS_ThrowReferenceError(ctx, "Could not find module '%s'", module_name);
                    return nullptr;
                }

                std::ifstream f(p, std::ios::binary | std::ios::ate);
                if (!f) return nullptr;

                std::streamsize size = f.tellg();
                f.seekg(0, std::ios::beg);
                std::string code(size, '\0');
                f.read(code.data(), size);

                JSValue func_val = JS_Eval(ctx, code.data(), code.size(),
                                           module_name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);

                if (JS_IsException(func_val)) return nullptr;

                auto* m = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(func_val));
                JS_FreeValue(ctx, func_val);
                return m;
            }, this);
        }

        ~Engine() {
            // Clean up any lingering rejection values
            if (!JS_IsUndefined(last_promise_rejection_)) {
                JS_FreeValue(ctx.get(), last_promise_rejection_);
            }
        }

        // --- Memory Management APIs ---

        // Triggers the QuickJS cycle collector to clean up unreferenced memory islands
        void run_gc() const {
            JS_RunGC(rt.get());
        }

        // --- Execution APIs ---

        std::expected<std::string, EngineError> eval(std::string_view code, std::string_view filename = "<eval>", const EvalMode mode = EvalMode::Script) const {
            int eval_flags = (mode == EvalMode::Module) ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL;
            JSValue ret = JS_Eval(ctx.get(), code.data(), code.size(), filename.data(), eval_flags);

            return handle_execution_result(ret);
        }

        std::expected<std::string, EngineError> eval_file(const std::filesystem::path& p, EvalMode mode = EvalMode::Script) const {
            std::ifstream f(p, std::ios::binary | std::ios::ate);

            if (!f) {
                return std::unexpected(EngineError{
                    .message = "File not found: " + p.string(),
                    .filename = p.string()
                });
            }

            std::streamsize size = f.tellg();
            f.seekg(0, std::ios::beg);
            std::string code(size, '\0');
            if (!f.read(code.data(), size)) {
                return std::unexpected(EngineError{
                    .message = "Failed to read file: " + p.string(),
                    .filename = p.string()
                });
            }

            return eval(code, p.string(), mode);
        }

        std::expected<std::string, EngineError> eval_bytecode(const uint8_t* bytecode, size_t len) const {
            const JSValue obj = JS_ReadObject(ctx.get(), bytecode, len, JS_READ_OBJ_BYTECODE);
            if (JS_IsException(obj)) return wrap_result(obj);

            JSValue ret = JS_EvalFunction(ctx.get(), obj);
            return handle_execution_result(ret);
        }

        // --- Loading/Registration APIs ---

        void register_module_source(const std::string& name, std::string_view code) {
            source_modules_[name] = std::string(code);
        }

        void register_module_bytecode(const std::string& name, const uint8_t* bytecode, size_t len) {
            bytecode_modules_[name] = { bytecode, len };
        }

        // --- Compilation APIs ---

        std::expected<std::vector<uint8_t>, EngineError> compile_file_to_bytecode(const std::filesystem::path& p, EvalMode mode = EvalMode::Module) const {
            std::ifstream f(p, std::ios::binary | std::ios::ate);

            if (!f) {
                return std::unexpected(EngineError{
                    .message = "File not found: " + p.string(),
                    .filename = p.string()
                });
            }

            std::streamsize size = f.tellg();
            f.seekg(0, std::ios::beg);
            std::string code(size, '\0');
            if (!f.read(code.data(), size)) {
                return std::unexpected(EngineError{
                    .message = "Failed to read the file",
                    .filename = p.string()
                });
            }

            int eval_flags = JS_EVAL_FLAG_COMPILE_ONLY;
            eval_flags |= (mode == EvalMode::Module) ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL;

            JSValue func_val = JS_Eval(ctx.get(), code.data(), code.size(), p.string().c_str(), eval_flags);

            if (JS_IsException(func_val)) {
                auto err = wrap_result(func_val);
                return std::unexpected(err.error());
            }

            size_t out_buf_len;
            uint8_t* out_buf = JS_WriteObject(ctx.get(), &out_buf_len, func_val, JS_WRITE_OBJ_BYTECODE);
            JS_FreeValue(ctx.get(), func_val);

            if (!out_buf) {
                return std::unexpected(EngineError{
                    .message = "Failed to serialize the bytecode",
                    .filename = p.string()
                });
            }

            std::vector<uint8_t> bytecode(out_buf, out_buf + out_buf_len);
            js_free(ctx.get(), out_buf);

            return bytecode;
        }

        // --- Make Stuff ---

        [[nodiscard]] Object make_object() const {
            return Object(Value(ctx.get(), JS_NewObject(ctx.get())));
        }

        template<typename T>
        Class<T> define_class(const std::string& name) {
            auto ctx_ptr = ctx.get();
            auto rt_ptr = rt.get();

            ClassRegistry<T>::init(rt_ptr, name);
            auto class_id = ClassRegistry<T>::class_id;

            init_wrapper_class(ctx_ptr);

            const Value proto_val(ctx_ptr, JS_NewObject(ctx_ptr));
            JS_SetClassProto(ctx_ptr, class_id, JS_DupValue(ctx_ptr, proto_val.get()));

            JSValue opaque_obj = JS_NewObjectClass(ctx_ptr, wrapper_class_id);
            auto* dispatcher = new ConstructorDispatcher<T>();
            JS_SetOpaque(opaque_obj, dispatcher);

            JSValue js_ctor = JS_NewCFunctionData(ctx_ptr,
                &ConstructorDispatcher<T>::apply,
                0, 0, 1, &opaque_obj
            );
            JS_FreeValue(ctx_ptr, opaque_obj);

            JS_SetConstructorBit(ctx_ptr, js_ctor, true);
            JS_SetConstructor(ctx_ptr, js_ctor, proto_val.get());

            JS_SetPropertyStr(ctx_ptr, JS_GetGlobalObject(ctx_ptr), name.c_str(), JS_DupValue(ctx_ptr, js_ctor));

            return Class<T>(Object(Value(ctx_ptr, js_ctor)), Object(proto_val), dispatcher);
        }

        Module define_module(const std::string& name) {
            auto m_def = std::make_unique<ModuleDef>();
            m_def->name = name;
            auto* def_ptr = m_def.get();
            modules_.push_back(std::move(m_def));

            JSModuleDef* js_module = JS_NewCModule(ctx.get(), name.c_str(), module_init_func);
            def_ptr->js_module = js_module;

            module_map_[js_module] = def_ptr;

            auto add_cb = [def_ptr](std::string exp_name, Value val) {
                def_ptr->exports.emplace_back(std::move(exp_name), std::move(val));
            };

            return Module(ctx.get(), js_module, std::move(add_cb));
        }

        [[nodiscard]] Object& global() {
            return global_wrapper;
        }

    private:
        struct RTDel { void operator()(JSRuntime* r) const { JS_FreeRuntime(r); } };
        struct CTDel { void operator()(JSContext* c) const { JS_FreeContext(c); } };
        std::unique_ptr<JSRuntime, RTDel> rt;
        std::unique_ptr<JSContext, CTDel> ctx;
        Object global_wrapper;

        // --- Module Registry Structs ---
        struct ModuleDef {
            std::string name;
            std::vector<std::pair<std::string, Value>> exports;
            JSModuleDef* js_module;
        };
        std::vector<std::unique_ptr<ModuleDef>> modules_;
        std::unordered_map<JSModuleDef*, ModuleDef*> module_map_;

        std::unordered_map<std::string, std::string> source_modules_;
        struct EmbeddedBytecode {
            const uint8_t* data;
            size_t len;
        };
        std::unordered_map<std::string, EmbeddedBytecode> bytecode_modules_;

        static int module_init_func(JSContext *ctx, JSModuleDef *m) {
            Engine* eng = static_cast<Engine*>(JS_GetContextOpaque(ctx));
            if (!eng) return -1;
            return eng->init_module_internal(m);
        }

        int init_module_internal(JSModuleDef* m) {
            auto it = module_map_.find(m);
            if (it == module_map_.end()) return -1;

            for (auto& [name, val] : it->second->exports) {
                JS_SetModuleExport(ctx.get(), m, name.c_str(), JS_DupValue(ctx.get(), val.get()));
            }
            return 0;
        }

        mutable JSValue last_promise_rejection_ = JS_UNDEFINED;

        static void promise_rejection_tracker(JSContext *ctx, JSValueConst /*promise*/, JSValueConst reason, bool is_handled, void *opaque) {
            Engine* eng = static_cast<Engine*>(opaque);
            if (!is_handled) {
                if (!JS_IsUndefined(eng->last_promise_rejection_)) {
                    JS_FreeValue(ctx, eng->last_promise_rejection_);
                }
                eng->last_promise_rejection_ = JS_DupValue(ctx, reason);
            } else {
                if (!JS_IsUndefined(eng->last_promise_rejection_)) {
                    JS_FreeValue(ctx, eng->last_promise_rejection_);
                    eng->last_promise_rejection_ = JS_UNDEFINED;
                }
            }
        }

        std::expected<std::string, EngineError> handle_execution_result(JSValue ret) const {
            if (JS_IsException(ret)) return wrap_result(ret);

            JSContext* pctx;
            int err;
            while ((err = JS_ExecutePendingJob(rt.get(), &pctx)) > 0) {}

            if (err < 0) {
                JSValue exception = JS_GetException(pctx);
                JS_FreeValue(ctx.get(), ret);

                // The event loop failed. Throw the extracted error object back.
                JS_Throw(ctx.get(), exception);
                return wrap_result(JS_EXCEPTION);
            }

            if (!JS_IsUndefined(last_promise_rejection_)) {
                JSValue rejected = JS_DupValue(ctx.get(), last_promise_rejection_);

                JS_FreeValue(ctx.get(), last_promise_rejection_);
                last_promise_rejection_ = JS_UNDEFINED;
                JS_FreeValue(ctx.get(), ret);

                // The module's promise rejected. Throw the rejected value back.
                JS_Throw(ctx.get(), rejected);
                return wrap_result(JS_EXCEPTION);
            }

            return wrap_result(ret);
        }

        std::expected<std::string, EngineError> wrap_result(const JSValue v) const {
            const Value managed_val(ctx.get(), v);

            if (JS_IsException(managed_val.get())) {
                JSValue exception = JS_GetException(ctx.get());
                EngineError err;

                err.message = converter<std::string>::get(ctx.get(), exception);

                if (JS_IsError(exception)) {
                    JSValue stack = JS_GetPropertyStr(ctx.get(), exception, "stack");
                    if (!JS_IsUndefined(stack)) {
                        err.stack = converter<std::string>::get(ctx.get(), stack);
                        JS_FreeValue(ctx.get(), stack);
                    }
                }

                JS_FreeValue(ctx.get(), exception);
                return std::unexpected(err);
            }

            return converter<std::string>::get(ctx.get(), managed_val.get());
        }

    };

} // namespace qjs
