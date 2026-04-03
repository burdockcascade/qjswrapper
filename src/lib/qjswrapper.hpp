#pragma once

// This wrapper is generic and does not depend on raylib

#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include <memory>
#include <functional>
#include <expected>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <typeindex>
#include <concepts>
#include <type_traits>

#include "quickjs.h"

namespace qjs {

    template<typename T>
    struct converter {
        static T get(JSContext* ctx, JSValueConst v) {
            if constexpr (std::is_same_v<T, bool>) {
                return JS_ToBool(ctx, v);
            } else if constexpr (std::integral<T>) {
                int32_t val = 0;
                JS_ToInt32(ctx, &val, v);
                return static_cast<T>(val);
            } else if constexpr (std::floating_point<T>) {
                double val = 0;
                JS_ToFloat64(ctx, &val, v);
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

        static JSValue put(JSContext* ctx, const T& val) {
            if constexpr (std::is_same_v<T, bool>) {
                return JS_NewBool(ctx, val);
            } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
                return JS_NewInt64(ctx, static_cast<int64_t>(val));
            } else if constexpr (std::floating_point<T>) {
                return JS_NewFloat64(ctx, static_cast<double>(val));
            } else if constexpr (std::is_convertible_v<T, std::string_view>) {
                return JS_NewStringLen(ctx, val.data(), val.size());
            }
            return JS_UNDEFINED;
        }
    };

    struct detail {
        static JSValue NewPtr(void* ptr) {
            return JS_NewInt64(nullptr, reinterpret_cast<int64_t>(ptr));
        }
        static void* ToPtr(JSValue v) {
            int64_t p;
            JS_ToInt64(nullptr, &p, v);
            return reinterpret_cast<void*>(p);
        }

        template <typename T>
        struct function_traits : function_traits<decltype(&T::operator())> {};

        template <typename ClassType, typename ReturnType, typename... Args>
        struct function_traits<ReturnType(ClassType::*)(Args...) const> {
            using type = std::function<ReturnType(Args...)>;
        };

        template <typename ClassType, typename ReturnType, typename... Args>
        struct function_traits<ReturnType(ClassType::*)(Args...)> {
            using type = std::function<ReturnType(Args...)>;
        };
    };

    template <typename R, typename... Args, size_t... I>
    static JSValue invoke_free_helper(JSContext* ctx, std::function<R(Args...)>& f, JSValueConst* argv, std::index_sequence<I...>) {
        if constexpr (std::is_void_v<R>) {
            f(converter<std::decay_t<Args>>::get(ctx, argv[I])...);
            return JS_UNDEFINED;
        } else {
            return converter<R>::put(ctx, f(converter<std::decay_t<Args>>::get(ctx, argv[I])...));
        }
    }

    class Engine;

    struct ConstructorEntry {
        int argc;
        std::function<void*(JSContext*, JSValueConst*)> invoker;
    };

    template <typename T>
    class ClassBinder {
        JSContext* ctx;
        JSValue proto;
        JSClassID class_id;
        std::string name;
        Engine& engine;

        struct ConstructorEntry {
            int argc;
            std::function<T*(JSContext*, JSValueConst*)> invoker;
        };
        std::vector<ConstructorEntry> ctors;

    public:
        ClassBinder(JSContext* c, JSValue p, JSClassID id, std::string_view n, Engine& e)
                : ctx(c), proto(p), class_id(id), name(n), engine(e) {}

        template <typename R, typename... Args>
        ClassBinder& method(this auto& self, std::string_view name, R (T::*func)(Args...)) {
            return self.template method_impl<R, Args...>(name, func);
        }

        template <typename R, typename... Args>
        ClassBinder& method(this auto& self, std::string_view name, R (T::*func)(Args...) const) {
            using NonConstFunc = R (T::*)(Args...);
            return self.template method_impl<R, Args...>(name, reinterpret_cast<NonConstFunc>(func));
        }

        template <typename F>
        requires (!std::is_member_function_pointer_v<std::decay_t<F>>)
        ClassBinder& method(std::string_view method_name, F&& f) {
            using Functor = detail::function_traits<std::decay_t<F>>::type;
            return method_lambda_impl(method_name, Functor(std::forward<F>(f)));
        }

        template <typename V>
        ClassBinder& field(std::string_view field_name, V T::*member);

        template <typename R, typename... Args>
        ClassBinder& static_method(std::string_view method_name, R (*func)(Args...));

        template<class ... Args>
        ClassBinder& constructor();

        template<class F>
        ClassBinder& constructor(F &&f);

        template <typename F>
        ClassBinder& static_method(std::string_view method_name, F&& f);

        template <typename V>
        ClassBinder& static_field(std::string_view field_name, V* data_ptr);

        template <typename V>
        ClassBinder& static_constant(std::string_view field_name, V value);

    private:
        template <typename R, typename... Args>
        ClassBinder& method_impl(std::string_view method_name, R (T::*func)(Args...));

        template <typename R, typename... Args>
        ClassBinder& method_lambda_impl(std::string_view method_name, std::function<R(T*, Args...)> f);

        ClassBinder& update_constructor_bit();

        template <typename R, typename... Args>
        ClassBinder& constructor_lambda_impl(std::function<R(Args...)> f);

        template <typename R, typename... Args>
        ClassBinder& static_method_lambda_impl(std::string_view method_name, std::function<R(Args...)> f);

        template <typename... Args, size_t... I>
        static T* ctor_helper(JSContext* ctx, JSValueConst* argv, std::index_sequence<I...>) {
            return new T(converter<std::decay_t<Args>>::get(ctx, argv[I])...);
        }
    };

    // ==== OBJECT BINDER ====

    class ObjectBinder {
        JSRuntime* rt;
        JSContext* ctx;
        JSValue obj_this;
        Engine& engine;
        std::string obj_name{};

    public:
        ObjectBinder(JSRuntime* r, JSContext* c, JSValue v, Engine& e, std::string n) : rt(r), ctx(c), obj_this(v), engine(e), obj_name(std::move(n)) {}

        template <typename T>
        auto register_class(std::string_view name);

        template<typename R, typename... Args>
        void register_function(std::string_view name, std::function<R(Args...)> f);

        template<typename F>
        void register_function(std::string_view name, F&& f);

        template<class T>
        void register_constant(std::string_view name, T value);

        inline ObjectBinder create_object(std::string_view name) const;

    };

    // ==== ENGINE ====

    class Engine {

    public:
        Engine() : rt(JS_NewRuntime()), ctx(JS_NewContext(rt.get())) {
            global_obj = JS_GetGlobalObject(ctx.get());
        }
        ~Engine() { JS_FreeValue(ctx.get(), global_obj); }

        // Script execution
        inline std::expected<std::string, std::string> eval_global(std::string_view code, std::string_view file) const;
        inline std::expected<std::string, std::string> eval_module(std::string_view code, std::string_view file) const;
        inline std::expected<std::string, std::string> run_file(const std::filesystem::path& p) const;
        inline std::expected<std::string, std::string> run_bytecode(const uint8_t* bytecode, size_t len) const;

        // Object binding
        inline ObjectBinder create_object(std::string_view name);
        inline ObjectBinder get_global_object();

        // Update track to accept shared_ptr
        template <typename T>
        void track(std::shared_ptr<T> p) {
            allocations.push_back(std::static_pointer_cast<void>(p));
        }

        // Keep an overload for unique_ptr if you still use it elsewhere
        template <typename T>
        void track(std::unique_ptr<T> p) {
            // Convert unique_ptr to shared_ptr for storage
            allocations.push_back(std::shared_ptr<T>(std::move(p)));
        }

        template <typename R, typename... Args, size_t... I>
        static R invoke_raw(JSContext* ctx, std::function<R(Args...)>& f, JSValueConst* argv, std::index_sequence<I...>) {
            return f(converter<std::decay_t<Args>>::get(ctx, argv[I])...);
        }

        template <typename T, typename R, typename... Args, size_t... I>
        static JSValue invoke_and_put(JSContext* ctx, T* inst, std::function<R(T*, Args...)>& f, JSValueConst* argv, std::index_sequence<I...>) {
            if constexpr (std::is_void_v<R>) {
                f(inst, converter<std::decay_t<Args>>::get(ctx, argv[I])...);
                return JS_UNDEFINED;
            } else {
                return converter<R>::put(ctx, f(inst, converter<std::decay_t<Args>>::get(ctx, argv[I])...));
            }
        }

    private:
        struct RTDel { void operator()(JSRuntime* r) const { JS_FreeRuntime(r); } };
        struct CTDel { void operator()(JSContext* c) const { JS_FreeContext(c); } };
        std::unique_ptr<JSRuntime, RTDel> rt;
        std::unique_ptr<JSContext, CTDel> ctx;
        JSValue global_obj{};
        std::vector<std::shared_ptr<void>> allocations;
        std::expected<std::string, std::string> wrap_result(JSValue v) const;

    };

    // ==== INLINE IMPLEMENTATION ====

    inline std::expected<std::string, std::string> Engine::eval_global(std::string_view code, std::string_view filename) const {
        return wrap_result(JS_Eval(ctx.get(), code.data(), code.size(), filename.data(), JS_EVAL_TYPE_GLOBAL));
    }

    inline std::expected<std::string, std::string> Engine::eval_module(std::string_view code, std::string_view filename) const {
        return wrap_result(JS_Eval(ctx.get(), code.data(), code.size(), filename.data(), JS_EVAL_TYPE_MODULE));
    }

    inline std::expected<std::string, std::string> Engine::run_bytecode(const uint8_t* bytecode, size_t len) const {
        const JSValue obj = JS_ReadObject(ctx.get(), bytecode, len, JS_READ_OBJ_BYTECODE);
        if (JS_IsException(obj)) return wrap_result(obj);
        return wrap_result(JS_EvalFunction(ctx.get(), obj));
    }

    inline std::expected<std::string, std::string> Engine::run_file(const std::filesystem::path& p) const {
        std::ifstream f(p);
        if (!f) return std::unexpected("File not found: " + p.string());
        std::stringstream b;
        b << f.rdbuf();
        return eval_global(b.str(), p.filename().string());
    }

    inline ObjectBinder Engine::create_object(std::string_view name) {
        const JSValue obj = JS_NewObject(ctx.get()); // Extracting raw pointer from unique_ptr
        JS_SetPropertyStr(ctx.get(), global_obj, name.data(), JS_DupValue(ctx.get(), obj));

        // Pass the raw JSContext* to the binder
        return ObjectBinder(rt.get(), ctx.get(), obj, *this, std::string(name));
    }

    inline ObjectBinder ObjectBinder::create_object(std::string_view name) const {
        const JSValue obj = JS_NewObject(ctx); // Extracting raw pointer from unique_ptr
        JS_SetPropertyStr(ctx, obj_this, name.data(), JS_DupValue(ctx, obj));

        // Pass the raw JSContext* to the binder
        return ObjectBinder(rt, ctx, obj, engine, std::string(name));
    }

    inline ObjectBinder Engine::get_global_object() {
        return ObjectBinder(rt.get(), ctx.get(), global_obj, *this, "global");
    }

    inline std::expected<std::string, std::string> Engine::wrap_result(const JSValue v) const {
        if (JS_IsException(v)) {
            const JSValue e = JS_GetException(ctx.get());
            std::string msg = converter<std::string>::get(ctx.get(), e);

            // Try to get the stack trace
            const JSValue stack = JS_GetPropertyStr(ctx.get(), e, "stack");
            if (!JS_IsUndefined(stack)) {
                msg += "\n" + converter<std::string>::get(ctx.get(), stack);
            }

            JS_FreeValue(ctx.get(), stack);
            JS_FreeValue(ctx.get(), e);
            JS_FreeValue(ctx.get(), v);
            return std::unexpected(msg);
        }
        std::string res = converter<std::string>::get(ctx.get(), v);
        JS_FreeValue(ctx.get(), v);
        return res;
    }

    // ==== TEMPLATE IMPLEMENTATIONS ====

    template <typename T>
    auto ObjectBinder::register_class(std::string_view name) {
        JSClassID id = 0;
        JS_NewClassID(rt, &id);

        // Define how JS objects of this class are deleted
        const JSClassDef def = { name.data(), [](JSRuntime* rt, JSValue val) {
            T* ptr = static_cast<T*>(JS_GetOpaque(val, 0));
            delete ptr;
        }};
        JS_NewClass(rt, id, &def);

        JSValue proto = JS_NewObject(ctx);
        JS_SetClassProto(ctx, id, proto);

        // Create the ClassBinder
        ClassBinder<T> binder(ctx, proto, id, name, engine);

        // Register a default constructor (no arguments) automatically
        if constexpr (std::is_default_constructible_v<T>) {
            binder.template constructor<>();
        }

        return binder;
    }

    template<typename R, typename... Args>
        void ObjectBinder::register_function(std::string_view name, std::function<R(Args...)> f) {
        using Func = std::function<R(Args...)>;
        auto wrap = std::make_unique<Func>(std::move(f));
        void* raw = wrap.get();
        engine.track(std::move(wrap));
        JSValue data = detail::NewPtr(raw);
        auto tramp = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
            auto* fn = static_cast<Func*>(detail::ToPtr(data[0]));
            return invoke_free_helper<R, Args...>(ctx, *fn, argv, std::make_index_sequence<sizeof...(Args)>{});
        };
        JS_SetPropertyStr(ctx, obj_this, name.data(), JS_NewCFunctionData(ctx, tramp, sizeof...(Args), 0, 1, &data));
    }

    template<typename F>
    void ObjectBinder::register_function(std::string_view name, F&& f) {
        register_function(name, std::function(std::forward<F>(f)));
    }

    template <typename T>
    template <typename R, typename... Args>
    ClassBinder<T>& ClassBinder<T>::method_impl(std::string_view method_name, R (T::*func)(Args...)) {
        using Wrapper = std::function<R(T*, Args...)>;
        auto wrap = std::make_unique<Wrapper>([func](T* i, Args... args) { return (i->*func)(args...); });
        void* raw_wrap = wrap.get();
        engine.track(std::move(wrap));
        auto trampoline = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
            auto* f = static_cast<Wrapper*>(detail::ToPtr(data[0]));
            int32_t id; JS_ToInt32(ctx, &id, data[1]);
            T* instance = static_cast<T*>(JS_GetOpaque(this_val, id));
            if (!instance) return JS_ThrowTypeError(ctx, "Invalid 'this'");
            return Engine::invoke_and_put<T, R, Args...>(ctx, instance, *f, argv, std::make_index_sequence<sizeof...(Args)>{});
        };
        JSValue data_arr[2] = { detail::NewPtr(raw_wrap), JS_NewInt32(ctx, class_id) };
        JSValue js_method = JS_NewCFunctionData(ctx, trampoline, sizeof...(Args), 0, 2, data_arr);
        JS_SetPropertyStr(ctx, proto, method_name.data(), js_method);
        return *this;
    }

    template <typename T>
    template <typename R, typename... Args>
    ClassBinder<T>& ClassBinder<T>::method_lambda_impl(std::string_view method_name, std::function<R(T*, Args...)> f) {
        auto wrap = std::make_unique<std::function<R(T*, Args...)>>(std::move(f));
        void* raw_wrap = wrap.get();
        engine.track(std::move(wrap));
        auto trampoline = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
            auto* fn = static_cast<std::function<R(T*, Args...)>*>(detail::ToPtr(data[0]));
            int32_t id; JS_ToInt32(ctx, &id, data[1]);
            T* instance = static_cast<T*>(JS_GetOpaque(this_val, id));
            if (!instance) return JS_ThrowTypeError(ctx, "Invalid 'this'");
            return Engine::invoke_and_put<T, R, Args...>(ctx, instance, *fn, argv, std::make_index_sequence<sizeof...(Args)>{});
        };
        JSValue data_arr[2] = { detail::NewPtr(raw_wrap), JS_NewInt32(ctx, class_id) };
        JSValue js_method = JS_NewCFunctionData(ctx, trampoline, sizeof...(Args), 0, 2, data_arr);
        JS_SetPropertyStr(ctx, proto, method_name.data(), js_method);
        return *this;
    }

    template <typename T>
    template <typename R, typename... Args>
    ClassBinder<T>& ClassBinder<T>::static_method(std::string_view method_name, R (*func)(Args...)) {
        using Wrapper = std::function<R(Args...)>;
        auto wrap = std::make_unique<Wrapper>([func](Args... args) { return func(args...); });
        void* raw_wrap = wrap.get();
        engine.track(std::move(wrap));

        auto trampoline = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
            auto* f = static_cast<Wrapper*>(detail::ToPtr(data[0]));
            return invoke_free_helper<R, Args...>(ctx, *f, argv, std::make_index_sequence<sizeof...(Args)>{});
        };

        JSValue data_val = detail::NewPtr(raw_wrap);
        JSValue js_method = JS_NewCFunctionData(ctx, trampoline, sizeof...(Args), 0, 1, &data_val);

        JSValue global = JS_GetGlobalObject(ctx);
        JSValue ctor = JS_GetPropertyStr(ctx, global, name.c_str());
        JS_SetPropertyStr(ctx, ctor, method_name.data(), js_method);

        JS_FreeValue(ctx, ctor);
        JS_FreeValue(ctx, global);
        return *this;
    }

    template <typename T>
    template <typename... Args>
    ClassBinder<T>& ClassBinder<T>::constructor() {
        ctors.push_back({
            static_cast<int>(sizeof...(Args)),
            [](JSContext* ctx, JSValueConst* argv) {
                return ctor_helper<Args...>(ctx, argv, std::make_index_sequence<sizeof...(Args)>{});
            }
        });
        return update_constructor_bit();
    }

    // Register a custom lambda constructor
    template <typename T>
    template <typename F>
    ClassBinder<T>& ClassBinder<T>::constructor(F&& f) {
        using Functor = detail::function_traits<std::decay_t<F>>::type;
        return constructor_lambda_impl(Functor(std::forward<F>(f)));
    }

    template <typename T>
    ClassBinder<T>& ClassBinder<T>::update_constructor_bit() {
        // Capture the ctors vector by moving it to a shared pointer so the JS closure can access it
        auto shared_ctors = std::make_shared<std::vector<ConstructorEntry>>(std::move(this->ctors));
        void* raw_ptr = shared_ctors.get();
        engine.track(shared_ctors);

        JSValue data[2] = { detail::NewPtr(raw_ptr), JS_NewInt32(ctx, class_id) };

        auto dispatcher = [](JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
            if (JS_IsUndefined(new_target)) return JS_ThrowTypeError(ctx, "Constructor must be called with 'new'");

            auto* entries = static_cast<std::vector<ConstructorEntry>*>(detail::ToPtr(data[0]));
            int32_t id; JS_ToInt32(ctx, &id, data[1]);

            T* instance = nullptr;
            for (const auto& entry : *entries) {
                if (entry.argc == argc) {
                    instance = entry.invoker(ctx, argv);
                    break;
                }
            }

            if (!instance) return JS_ThrowTypeError(ctx, "No constructor found with %d arguments", argc);

            JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
            JSValue obj = JS_NewObjectClass(ctx, id);
            JS_SetPrototype(ctx, obj, proto);
            JS_FreeValue(ctx, proto);
            JS_SetOpaque(obj, instance);
            return obj;
        };

        JSValue ctor_func = JS_NewCFunctionData(ctx, dispatcher, 0, 0, 2, data);
        JS_SetConstructorBit(ctx, ctor_func, true);

        JSValue global = JS_GetGlobalObject(ctx);
        JS_SetPropertyStr(ctx, global, name.c_str(), ctor_func);
        JS_FreeValue(ctx, global);

        // Keep the internal list for further additions if needed
        this->ctors = *shared_ctors;
        return *this;
    }

    template <typename T>
    template <typename R, typename... Args>
    ClassBinder<T>& ClassBinder<T>::constructor_lambda_impl(std::function<R(Args...)> f) {
        static_assert(std::is_same_v<R, T*>, "Constructor lambda must return T*");

        auto wrap = std::make_shared<std::function<R(Args...)>>(std::move(f));
        engine.track(wrap);

        ctors.push_back({
            static_cast<int>(sizeof...(Args)),
            [wrap](JSContext* ctx, JSValueConst* argv) {
                return Engine::invoke_raw<R, Args...>(ctx, *wrap, argv, std::make_index_sequence<sizeof...(Args)>{});
            }
        });

        return update_constructor_bit();
    }

    template <typename T>
    template <typename V>
    ClassBinder<T>& ClassBinder<T>::field(std::string_view field_name, V T::*member) {
        struct FieldAccessor { V T::*ptr; JSClassID id; };
        auto acc = std::make_unique<FieldAccessor>(member, class_id);
        void* raw_acc = acc.get();
        engine.track(std::move(acc));

        auto getter = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
            auto* acc = static_cast<FieldAccessor*>(detail::ToPtr(data[0]));
            T* instance = static_cast<T*>(JS_GetOpaque(this_val, acc->id));
            if (!instance) return JS_ThrowTypeError(ctx, "Invalid 'this'");
            return converter<V>::put(ctx, instance->*(acc->ptr));
        };

        auto setter = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
            auto* acc = static_cast<FieldAccessor*>(detail::ToPtr(data[0]));
            T* instance = static_cast<T*>(JS_GetOpaque(this_val, acc->id));
            if (!instance) return JS_ThrowTypeError(ctx, "Invalid 'this'");
            instance->*(acc->ptr) = converter<V>::get(ctx, argv[0]);
            return JS_UNDEFINED;
        };

        JSValue data_val = detail::NewPtr(raw_acc);
        const JSValue js_get = JS_NewCFunctionData(ctx, getter, 0, 0, 1, &data_val);
        const JSValue js_set = JS_NewCFunctionData(ctx, setter, 1, 0, 1, &data_val);
        const JSAtom atom = JS_NewAtom(ctx, field_name.data());
        JS_DefinePropertyGetSet(ctx, proto, atom, js_get, js_set, JS_PROP_C_W_E);

        // Cleanup temporary JS references
        JS_FreeAtom(ctx, atom);

        return *this;
    }

    template <typename T>
    template <typename V>
    ClassBinder<T>& ClassBinder<T>::static_field(std::string_view field_name, V* data_ptr) {

        auto getter = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
            V* ptr = static_cast<V*>(detail::ToPtr(data[0]));
            return converter<V>::put(ctx, *ptr);
        };

        auto setter = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
            V* ptr = static_cast<V*>(detail::ToPtr(data[0]));
            *ptr = converter<V>::get(ctx, argv[0]);
            return JS_UNDEFINED;
        };

        // Package the pointer into JS-accessible data
        JSValue data_val = detail::NewPtr(data_ptr);
        const JSValue js_get = JS_NewCFunctionData(ctx, getter, 0, 0, 1, &data_val);
        const JSValue js_set = JS_NewCFunctionData(ctx, setter, 1, 0, 1, &data_val);

        // Look up the Constructor on the Global Object
        const JSValue global = JS_GetGlobalObject(ctx);
        const JSValue ctor = JS_GetPropertyStr(ctx, global, name.c_str());
        const JSAtom atom = JS_NewAtom(ctx, field_name.data());

        // Define the property on the constructor (static access)
        JS_DefinePropertyGetSet(ctx, ctor, atom, js_get, js_set, JS_PROP_C_W_E);

        // Cleanup temporary JS references
        JS_FreeAtom(ctx, atom);
        JS_FreeValue(ctx, ctor);
        JS_FreeValue(ctx, global);

        return *this;
    }

    template <typename T>
    template <typename V>
    ClassBinder<T>& ClassBinder<T>::static_constant(const std::string_view field_name, V value) {
        // We store the value in a shared_ptr so the JS callback can access it forever
        auto saved_value = std::make_shared<V>(value);

        auto getter = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
            // Retrieve the stored value from our heap-allocated shared_ptr
            V* ptr = static_cast<V*>(detail::ToPtr(data[0]));
            return converter<V>::put(ctx, *ptr);
        };

        // No setter because it's a constant
        JSValue data_val = detail::NewPtr(saved_value.get());

        // Track the shared_ptr so it isn't deleted while the engine is running
        engine.track(std::move(saved_value));

        const JSValue js_get = JS_NewCFunctionData(ctx, getter, 0, 0, 1, &data_val);

        const JSValue global = JS_GetGlobalObject(ctx);
        const JSValue ctor = JS_GetPropertyStr(ctx, global, name.c_str());
        const JSAtom atom = JS_NewAtom(ctx, field_name.data());

        // Register as Read-Only (JS_PROP_C_W_E means Configurable, NOT Writable, Enumerable)
        JS_DefinePropertyGetSet(ctx, ctor, atom, js_get, JS_UNDEFINED, JS_PROP_WRITABLE | JS_PROP_ENUMERABLE);

        JS_FreeAtom(ctx, atom);
        JS_FreeValue(ctx, ctor);
        JS_FreeValue(ctx, global);
        return *this;
    }

    template <typename T>
    template <typename F>
    ClassBinder<T>& ClassBinder<T>::static_method(std::string_view method_name, F&& f) {
        using Functor = detail::function_traits<std::decay_t<F>>::type;
        return static_method_lambda_impl(method_name, Functor(std::forward<F>(f)));
    }

    template <typename T>
    template <typename R, typename... Args>
    ClassBinder<T>& ClassBinder<T>::static_method_lambda_impl(std::string_view method_name, std::function<R(Args...)> f) {
        auto wrap = std::make_unique<std::function<R(Args...)>>(std::move(f));
        void* raw_wrap = wrap.get();
        engine.track(std::move(wrap));

        auto trampoline = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
            auto* fn = static_cast<std::function<R(Args...)>*>(detail::ToPtr(data[0]));
            return invoke_free_helper<R, Args...>(ctx, *fn, argv, std::make_index_sequence<sizeof...(Args)>{});
        };

        JSValue data_arr[1] = { detail::NewPtr(raw_wrap) };
        const JSValue js_method = JS_NewCFunctionData(ctx, trampoline, sizeof...(Args), 0, 1, data_arr);

        const JSValue global = JS_GetGlobalObject(ctx);
        const JSValue ctor = JS_GetPropertyStr(ctx, global, name.c_str());
        JS_SetPropertyStr(ctx, ctor, method_name.data(), js_method);

        JS_FreeValue(ctx, ctor);
        JS_FreeValue(ctx, global);
        return *this;
    }

    template <typename T>
    void ObjectBinder::register_constant(std::string_view name, T value) {
        const JSValue val = converter<T>::put(ctx, value);
        JS_DefinePropertyValueStr(ctx, obj_this, name.data(), val, JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE);
    }

} // namespace qjs