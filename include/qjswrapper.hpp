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
#include <sstream>
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
        using function_type = R(Args...); // <--- Add this
        static constexpr size_t arity = sizeof...(Args);
    };

    // Do the same for the member function pointer specializations:
    template<typename C, typename R, typename... Args>
    struct function_traits<R(C::*)(Args...) const> {
        using return_type = R;
        using args_tuple = std::tuple<Args...>;
        using function_type = R(Args...); // <--- Add this
        static constexpr size_t arity = sizeof...(Args);
    };

    template<typename C, typename R, typename... Args>
    struct function_traits<R(C::*)(Args...)> {
        using return_type = R;
        using args_tuple = std::tuple<Args...>;
        using function_type = R(Args...); // Added
        static constexpr size_t arity = sizeof...(Args);
    };


    // --- Concepts ---
    template<typename T>
    concept is_class_type = std::is_class_v<std::remove_cvref_t<T>>;

    template<typename T>
    concept callable = (is_class_type<T> && requires(T t) {
        &std::remove_cvref_t<T>::operator();
    }) || std::is_function_v<std::remove_pointer_t<std::remove_cvref_t<T>>>;

    struct CallableBase {
        virtual ~CallableBase() = default;
    };

    // 2. Strongly-typed wrapper holding the specific std::function
    template<typename FuncType>
    struct CallableWrapper : CallableBase {
        std::function<FuncType> func;
        explicit CallableWrapper(std::function<FuncType> f) : func(std::move(f)) {}
    };

    // --- Bridge Components ---
    // Defined as inline to prevent multiple definition errors across translation units
    inline JSClassID wrapper_class_id = 0;

    template<typename R, typename ArgsTuple>
    struct Invoker;

    template<typename R, typename... Args>
    struct Invoker<R, std::tuple<Args...>> {
        static JSValue apply(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) {
            void* p = JS_GetOpaque(data[0], wrapper_class_id);
            if (!p) {
                return JS_ThrowTypeError(ctx, "Failed to retrieve C++ lambda closure");
            }

            // 3. Cast the opaque pointer back to our specific wrapper type
            auto* wrapper = static_cast<CallableWrapper<R(Args...)>*>(p);
            auto& func = wrapper->func;

            auto args = [&]<size_t... Is>(std::index_sequence<Is...>) {
                return std::make_tuple(converter<std::decay_t<Args>>::get(ctx, (Is < argc ? argv[Is] : JS_UNDEFINED))...);
            }(std::index_sequence_for<Args...>{});

            try {
                if constexpr (std::is_void_v<R>) {
                    std::apply(func, std::move(args));
                    return JS_UNDEFINED;
                } else {
                    R result = std::apply(func, std::move(args));
                    Value ret = converter<R>::put(ctx, result);
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

            // Check if class is registered for this runtime
            if (!JS_IsRegisteredClass(rt, class_id)) {
                JSClassDef def{
                    .class_name = name.c_str(),
                    .finalizer = [](JSRuntime* rt, JSValue val) {
                        auto* obj = static_cast<T*>(JS_GetOpaque(val, class_id));
                        delete obj; // JS GC calls C++ destructor
                    }
                };
                JS_NewClass(rt, class_id, &def);
            }
        }
    };

    template<typename T, typename R, typename... Args>
    struct MemberInvoker {
        using MemberFunc = R(T::*)(Args...);

        static JSValue apply(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) {
            // 1. Retrieve the C++ instance pointer from the 'this' object
            // We use the unique ClassID for type T to ensure safety
            T* instance = static_cast<T*>(JS_GetOpaque(this_val, ClassRegistry<T>::class_id));

            if (!instance) {
                return JS_ThrowTypeError(ctx, "Method called on incompatible object (expected C++ instance)");
            }

            // 2. Retrieve the member function pointer from the 'data' array
            // The function pointer was wrapped in an opaque object by the Class Builder
            void* raw_func = JS_GetOpaque(data[0], wrapper_class_id);
            if (!raw_func) {
                return JS_ThrowTypeError(ctx, "Failed to retrieve C++ member function pointer");
            }

            auto* method_ptr = static_cast<MemberFunc*>(raw_func);

            // 3. Convert JavaScript arguments to C++ types using your converter
            auto args = [&]<size_t... Is>(std::index_sequence<Is...>) {
                return std::make_tuple(converter<std::decay_t<Args>>::get(ctx, (Is < argc ? argv[Is] : JS_UNDEFINED))...);
            }(std::index_sequence_for<Args...>{});

            // 4. Execute the call and handle the return value
            try {
                if constexpr (std::is_void_v<R>) {
                    std::apply(*method_ptr, instance, std::move(args));
                    return JS_UNDEFINED;
                } else {
                    R result = std::apply(*method_ptr, instance, std::move(args));
                    // Return a duplicated value to ensure the C++ wrapper doesn't
                    // free it prematurely
                    Value ret = converter<R>::put(ctx, result);
                    return JS_DupValue(ctx, ret.get());
                }
            } catch (const std::exception& e) {
                return JS_ThrowInternalError(ctx, "%s", e.what());
            } catch (...) {
                return JS_ThrowInternalError(ctx, "Unknown C++ exception in member function");
            }
        }
    };

    // 3. Strongly-typed wrapper holding member function pointers
    template<typename MemFunc>
    struct MemberCallableWrapper : CallableBase {
        MemFunc func;
        explicit MemberCallableWrapper(MemFunc f) : func(f) {}
    };

    // Helper to safely initialize the Lambda storage class across all function types
    inline void init_wrapper_class(JSContext* ctx) {
        auto rt = JS_GetRuntime(ctx);
        if (wrapper_class_id == 0) {
            JS_NewClassID(rt, &wrapper_class_id);
        }
        if (!JS_IsRegisteredClass(rt, wrapper_class_id)) {
            JSClassDef def{
                .class_name = "CppLambda",
                .finalizer = [](JSRuntime* rt, JSValue val) {
                    auto* base = static_cast<CallableBase*>(JS_GetOpaque(val, wrapper_class_id));
                    delete base;
                }
            };
            JS_NewClass(rt, wrapper_class_id, &def);
        }
    }

    template<typename MethodType, typename C, typename R, typename... Args>
    struct MemberInvokerImpl {
        static JSValue apply(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) {
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
                // Apply the extracted arguments via a small lambda to pair them with the instance pointer
                if constexpr (std::is_void_v<R>) {
                    std::apply([instance, method_ptr](auto&&... unpacked) {
                        (instance->*method_ptr)(std::forward<decltype(unpacked)>(unpacked)...);
                    }, std::move(args));
                    return JS_UNDEFINED;
                } else {
                    R result = std::apply([instance, method_ptr](auto&&... unpacked) {
                        return (instance->*method_ptr)(std::forward<decltype(unpacked)>(unpacked)...);
                    }, std::move(args));
                    Value ret = converter<R>::put(ctx, result);
                    return JS_DupValue(ctx, ret.get());
                }
            } catch (const std::exception& e) {
                return JS_ThrowInternalError(ctx, "%s", e.what());
            } catch (...) {
                return JS_ThrowInternalError(ctx, "Unknown exception in member function");
            }
        }
    };

    // Replace the existing create_js_function with this setup supporting standard lambdas...
    template<typename F>
    requires callable<F>
    JSValue create_js_function(JSContext* ctx, F&& func) {
        using traits = function_traits<std::decay_t<F>>;
        using R = typename traits::return_type;
        using ArgsTuple = typename traits::args_tuple;
        using FuncType = typename traits::function_type;

        init_wrapper_class(ctx);

        JSValue opaque_obj = JS_NewObjectClass(ctx, wrapper_class_id);
        auto* func_ptr = new CallableWrapper<FuncType>(std::forward<F>(func));
        JS_SetOpaque(opaque_obj, func_ptr);

        JSValue js_func = JS_NewCFunctionData(ctx,
            &Invoker<R, ArgsTuple>::apply,
            static_cast<int>(traits::arity), 0, 1, &opaque_obj
        );

        JS_FreeValue(ctx, opaque_obj);
        return js_func;
    }

    // ...and add these two new overloads to support const and non-const class member functions!
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

        static JSValue apply(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) {
            void* p = JS_GetOpaque(data[0], wrapper_class_id);
            if (!p) return JS_ThrowTypeError(ctx, "Failed to retrieve constructor dispatcher");

            auto* dispatcher = static_cast<ConstructorDispatcher<T>*>(p);

            // Iterate through registered overloads to find one matching the argument count
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

        // 1. Unified Set for primitives/values
        template<typename T>
        requires (!callable<T> && !std::is_member_function_pointer_v<std::decay_t<T>>)
        Object& set(std::string_view prop, T&& value, Prop mode = Prop::Normal) {
            auto ctx = val_.ctx();
            const Value v = converter<std::decay_t<T>>::put(ctx, std::forward<T>(value));
            const int flags = resolve_flags(mode);
            JS_DefinePropertyValueStr(ctx, val_.get(), prop.data(), JS_DupValue(ctx, v.get()), flags);
            return *this;
        }

        // 2. Unified Set for C++ lambdas and member functions
        template<typename F>
        requires (callable<F> || std::is_member_function_pointer_v<std::decay_t<F>>)
        Object& set(std::string_view prop, F&& func, Prop mode = Prop::ReadOnly) {
            const auto ctx = val_.ctx();
            const JSValue js_func = create_js_function(ctx, std::forward<F>(func));

            const int flags = resolve_flags(mode);
            JS_DefinePropertyValueStr(ctx, val_.get(), prop.data(), js_func, flags);
            return *this;
        }

        // Get
        template<typename T>
        T get(const std::string_view prop) const {
            auto ctx = val_.ctx();
            JSValue p = JS_GetPropertyStr(ctx, val_.get(), prop.data());
            T result = converter<T>::get(ctx, p);
            JS_FreeValue(ctx, p);
            return result;
        }

        [[nodiscard]] bool remove(std::string_view prop) const {
            const auto ctx = val_.ctx();
            const JSAtom atom = JS_NewAtomLen(ctx, prop.data(), prop.size());

            // JS_DeleteProperty returns 1 if successful, 0 if not (or if non-configurable)
            const int ret = JS_DeleteProperty(ctx, val_.get(), atom, 0);
            JS_FreeAtom(ctx, atom);

            return ret == 1;
        }

        template<typename... Args>
        Value invoke(const std::string_view prop, Args&&... args) const {
            auto ctx = val_.ctx();

            // 1. Get the function property
            const JSValue func = JS_GetPropertyStr(ctx, val_.get(), prop.data());
            if (!JS_IsFunction(ctx, func)) {
                JS_FreeValue(ctx, func);
                // Return an exception or undefined depending on your error handling preference
                return { ctx, JS_ThrowTypeError(ctx, "Property is not a function") };
            }

            // 2. Convert C++ arguments to JSValues using your converter
            std::vector<Value> managed_args;
            (managed_args.push_back(converter<std::decay_t<Args>>::put(ctx, std::forward<Args>(args))), ...);

            std::vector<JSValue> raw_args;
            for (const auto& arg : managed_args) raw_args.push_back(arg.get());

            // 3. Call the function natively (val_.get() acts as the 'this' context!)
            JSValue result = JS_Call(ctx, func, val_.get(), raw_args.size(), raw_args.data());

            JS_FreeValue(ctx, func);
            return { ctx, result }; // Wrap the result so memory is managed
        }

        [[nodiscard]] std::vector<std::string> keys() const {
            const auto ctx = val_.ctx();
            JSPropertyEnum *ptab = nullptr;
            uint32_t plen = 0;
            std::vector<std::string> result;

            // Get only enumerable properties (like Object.keys())
            if (JS_GetOwnPropertyNames(ctx, &ptab, &plen, val_.get(), JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
                for(uint32_t i = 0; i < plen; i++) {
                    if (const char* str = JS_AtomToCString(ctx, ptab[i].atom)) {
                        result.emplace_back(str);
                        JS_FreeCString(ctx, str);
                    }
                    JS_FreeAtom(ctx, ptab[i].atom); // Free each atom
                }
                // QuickJS requires using js_free for the property array itself
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

 // Included to access ConstructorDispatcher and function_traits

namespace qjs {

    template<typename T>
    class Class {
    public:
        // Takes ownership of the JS Constructor function, JS Prototype object, and dispatcher
        Class(Object constructor, Object prototype, ConstructorDispatcher<T>* dispatcher)
            : constructor_(std::move(constructor)), prototype_(std::move(prototype)), dispatcher_(dispatcher) {}

        // Standard constructor registration
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

        // Custom constructor registration (Factory lambda)
        template<typename F>
        requires callable<F>
        Class& constructor(F&& func) {
            using traits = function_traits<std::decay_t<F>>;
            register_custom_factory(std::forward<F>(func), typename traits::args_tuple{});
            return *this;
        }

        // method: Binds an instance method to the prototype
        template<typename F>
        requires (callable<F> || std::is_member_function_pointer_v<std::decay_t<F>>)
        Class& method(std::string_view name, F&& func) {
            prototype_.set(name, std::forward<F>(func));
            return *this;
        }

        // static_method: Binds a static method to the constructor
        template<typename F>
        requires callable<F>
        Class& static_method(std::string_view name, F&& func) {
            constructor_.set(name, std::forward<F>(func));
            return *this;
        }

        // variable: Binds a mutable default value to the prototype
        template<typename V>
        requires (!callable<V> && !std::is_member_function_pointer_v<std::decay_t<V>>)
        Class& variable(std::string_view name, V&& value) {
            prototype_.set(name, std::forward<V>(value), Prop::Normal);
            return *this;
        }

        // constant: Binds a read-only value to the prototype
        template<typename V>
        requires (!callable<V> && !std::is_member_function_pointer_v<std::decay_t<V>>)
        Class& constant(std::string_view name, V&& value) {
            prototype_.set(name, std::forward<V>(value), Prop::ReadOnly);
            return *this;
        }

        // static_variable: Binds a mutable static value to the constructor
        // (Added to maintain symmetry with static_constant!)
        template<typename V>
        requires (!callable<V> && !std::is_member_function_pointer_v<std::decay_t<V>>)
        Class& static_variable(std::string_view name, V&& value) {
            constructor_.set(name, std::forward<V>(value), Prop::Normal);
            return *this;
        }

        // static_constant: Binds a read-only static value to the constructor
        template<typename V>
        requires (!callable<V> && !std::is_member_function_pointer_v<std::decay_t<V>>)
        Class& static_constant(std::string_view name, V&& value) {
            constructor_.set(name, std::forward<V>(value), Prop::ReadOnly);
            return *this;
        }

        // Expose the constructor so it can be added to modules or objects directly
        [[nodiscard]] const Object& get_constructor() const {
            return constructor_;
        }

    private:
        Object constructor_;
        Object prototype_;
        ConstructorDispatcher<T>* dispatcher_;

        // Helper to deduce the argument types and wire up a custom lambda
        template<typename F, typename... Args>
        void register_custom_factory(F func, std::tuple<Args...>) {
            using traits = function_traits<std::decay_t<F>>;
            using Ret = typename traits::return_type;
            constexpr size_t arity = traits::arity;

            auto factory = [f = std::move(func)](JSContext* ctx, int argc, JSValueConst* argv) -> T* {
                auto args = [&]<size_t... Is>(std::index_sequence<Is...>) {
                    return std::make_tuple(converter<std::decay_t<Args>>::get(ctx, (Is < argc ? argv[Is] : JS_UNDEFINED))...);
                }(std::index_sequence_for<Args...>{});

                // If the lambda returns a T*, just use it. If it returns T by value, wrap it in 'new'
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

}






namespace qjs {

    class Module {
    public:
        Module(JSContext* ctx, JSModuleDef* m, std::function<void(std::string, Value)> add_cb)
            : ctx_(ctx), m_(m), add_cb_(std::move(add_cb)) {}

        // 1. Export primitives, variables, and standard Objects
        template<typename T>
        requires (!callable<T> && !std::is_member_function_pointer_v<std::decay_t<T>>)
        Module& add(const std::string& name, T&& value) {
            JS_AddModuleExport(ctx_, m_, name.c_str());
            add_cb_(name, converter<std::decay_t<T>>::put(ctx_, std::forward<T>(value)));
            return *this;
        }

        // 2. Export C++ lambdas and functions
        template<typename F>
        requires (callable<F> || std::is_member_function_pointer_v<std::decay_t<F>>)
        Module& add(const std::string& name, F&& func) {
            JS_AddModuleExport(ctx_, m_, name.c_str());
            const JSValue js_func = create_js_function(ctx_, std::forward<F>(func));
            add_cb_(name, Value(ctx_, js_func));
            return *this;
        }

        // 3a. Export a registered C++ Class (Non-const lvalue)
        template<typename T>
        Module& add(const std::string& name, Class<T>& cls) {
            return add(name, cls.get_constructor());
        }

        // 3b. Export a registered C++ Class (Const lvalue / rvalue)
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

    class Engine {

    public:
        Engine() : rt(JS_NewRuntime()), ctx(JS_NewContext(rt.get())), global_wrapper(Value(ctx.get(), JS_GetGlobalObject(ctx.get()))) {
            JS_SetContextOpaque(ctx.get(), this);

            // Updated Module Loader: Handles C++ Native, Embedded Bytecode, and Source files
            JS_SetModuleLoaderFunc(rt.get(), nullptr, [](JSContext* ctx, const char* module_name, void* opaque) -> JSModuleDef* {
                Engine* eng = static_cast<Engine*>(opaque);
                std::string name_str(module_name);

                // 1. Check Native C++ modules
                for (const auto& mod : eng->modules_) {
                    if (mod->name == name_str) {
                        return mod->js_module;
                    }
                }

                // 2. Check Embedded Bytecode modules
                auto b_it = eng->bytecode_modules_.find(name_str);
                if (b_it != eng->bytecode_modules_.end()) {
                    // Read the precompiled bytecode directly from memory
                    JSValue obj = JS_ReadObject(ctx, b_it->second.data, b_it->second.len, JS_READ_OBJ_BYTECODE);
                    if (JS_IsException(obj)) {
                        return nullptr;
                    }

                    // Extract the module definition pointer and free the wrapper value
                    auto* m = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(obj));
                    JS_FreeValue(ctx, obj);
                    return m;
                }

                // 3. Fallback: Try loading as a standard JS file from the filesystem
                std::filesystem::path p(name_str);
                if (!std::filesystem::exists(p)) {
                    JS_ThrowReferenceError(ctx, "Could not find module '%s'", module_name);
                    return nullptr;
                }

                std::ifstream f(p, std::ios::binary | std::ios::ate);
                if (!f) return nullptr;

                std::streamsize size = f.tellg();
                f.seekg(0, std::ios::beg);
                std::vector<uint8_t> buffer(size);
                f.read(reinterpret_cast<char*>(buffer.data()), size);

                // Compile source file into a module (but do not evaluate it yet)
                JSValue func_val = JS_Eval(ctx, reinterpret_cast<const char*>(buffer.data()), buffer.size(),
                                           module_name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);

                if (JS_IsException(func_val)) return nullptr;

                auto* m = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(func_val));
                JS_FreeValue(ctx, func_val);
                return m;
            }, this);
        }

        ~Engine() = default;

        // Script execution
        std::expected<std::string, std::string> eval_global(const std::string_view code, std::string_view filename) const {
            return wrap_result(JS_Eval(ctx.get(), code.data(), code.size(), filename.data(), JS_EVAL_TYPE_GLOBAL));
        }

        std::expected<std::string, std::string> eval_module(const std::string_view code, std::string_view filename) const {
            JSValue ret = JS_Eval(ctx.get(), code.data(), code.size(), filename.data(), JS_EVAL_TYPE_MODULE);

            // Catch immediate syntax errors
            if (JS_IsException(ret)) {
                return wrap_result(ret);
            }

            // 2. Spin the Event Loop! Modules run as Promises, so we must execute pending jobs.
            JSContext* pctx;
            int err;
            while ((err = JS_ExecutePendingJob(rt.get(), &pctx)) > 0) {
                // Keep looping until the job queue is completely empty
            }

            // If a runtime error happened during the job, catch it
            if (err < 0) {
                JSValue exception = JS_GetException(pctx);
                JS_FreeValue(ctx.get(), ret);  // Clean up the original promise
                return wrap_result(exception); // Wrap and return the job error
            }

            return wrap_result(ret);
        }

        std::expected<std::string, std::string> run_file(const std::filesystem::path& p) const {
            const std::ifstream f(p);
            if (!f) return std::unexpected("File not found: " + p.string());
            std::stringstream b;
            b << f.rdbuf();
            return eval_global(b.str(), p.filename().string());
        }

        std::expected<std::string, std::string> run_bytecode(const uint8_t* bytecode, size_t len) const {
            const JSValue obj = JS_ReadObject(ctx.get(), bytecode, len, JS_READ_OBJ_BYTECODE);
            if (JS_IsException(obj)) return wrap_result(obj);
            return wrap_result(JS_EvalFunction(ctx.get(), obj));
        }

        void register_bytecode_module(const std::string& name, const uint8_t* bytecode, size_t len) {
            bytecode_modules_[name] = { bytecode, len };
        }

        // Compiles a JavaScript file into QuickJS bytecode and returns it
        // The as_module flag determines if it should be compiled as an ES6 module
        std::expected<std::vector<uint8_t>, std::string> compile_file_to_bytecode(const std::filesystem::path& p, const bool as_module = true) const {
            // 1. Read the JS source file
            std::ifstream f(p, std::ios::binary | std::ios::ate);
            if (!f) return std::unexpected("File not found: " + p.string());

            std::streamsize size = f.tellg();
            f.seekg(0, std::ios::beg);

            std::string code(size, '\0');
            if (!f.read(code.data(), size)) {
                return std::unexpected("Failed to read file: " + p.string());
            }

            // 2. Determine compilation flags
            int eval_flags = JS_EVAL_FLAG_COMPILE_ONLY;
            eval_flags |= as_module ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL;

            // 3. Compile the JS code into a QuickJS function object
            JSValue func_val = JS_Eval(ctx.get(), code.data(), code.size(), p.string().c_str(), eval_flags);

            // Handle syntax errors during compilation
            if (JS_IsException(func_val)) {
                auto err = wrap_result(func_val);
                // wrap_result returns unexpected on exception, so we extract the error string
                return std::unexpected(err.error());
            }

            // 4. Serialize the compiled function object to a raw bytecode buffer
            size_t out_buf_len;
            uint8_t* out_buf = JS_WriteObject(ctx.get(), &out_buf_len, func_val, JS_WRITE_OBJ_BYTECODE);

            // Free the function object as we no longer need it
            JS_FreeValue(ctx.get(), func_val);

            if (!out_buf) {
                return std::unexpected("Failed to serialize bytecode (Out of memory or unsupported feature)");
            }

            // 5. Copy to a std::vector to manage the memory safely in C++
            std::vector<uint8_t> bytecode(out_buf, out_buf + out_buf_len);

            // Free the QuickJS-allocated buffer
            js_free(ctx.get(), out_buf);

            return bytecode;
        }

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

        // Define a new Native Module
        Module define_module(const std::string& name) {
            auto m_def = std::make_unique<ModuleDef>();
            m_def->name = name;
            auto* def_ptr = m_def.get();
            modules_.push_back(std::move(m_def));

            JSModuleDef* js_module = JS_NewCModule(ctx.get(), name.c_str(), module_init_func);
            def_ptr->js_module = js_module; // <-- Save the module pointer!

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
            JSModuleDef* js_module; // Added to map back in the loader
        };
        std::vector<std::unique_ptr<ModuleDef>> modules_;
        std::unordered_map<JSModuleDef*, ModuleDef*> module_map_;

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

        std::expected<std::string, std::string> wrap_result(const JSValue v) const {
            const Value managed_val(ctx.get(), v);

            if (JS_IsException(managed_val.get())) {
                const JSValue exception = JS_GetException(ctx.get());
                const Value managed_exc(ctx.get(), exception);
                return std::unexpected(converter<std::string>::get(ctx.get(), managed_exc.get()));
            }

            return converter<std::string>::get(ctx.get(), managed_val.get());
        }

        struct EmbeddedBytecode {
            const uint8_t* data;
            size_t len;
        };
        std::unordered_map<std::string, EmbeddedBytecode> bytecode_modules_;

    };

} // namespace qjs
