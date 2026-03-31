#pragma once

#include <any>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <expected>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <map>
#include <typeindex>

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
            } else if constexpr (std::integral<T>) {
                return JS_NewInt32(ctx, static_cast<int32_t>(val));
            } else if constexpr (std::floating_point<T>) {
                return JS_NewFloat64(ctx, static_cast<double>(val));
            } else if constexpr (std::is_convertible_v<T, std::string_view>) {
                return JS_NewStringLen(ctx, val.data(), val.size());
            }
            return JS_UNDEFINED;
        }
    };

    template <typename T>
    class ClassBinder {
        JSContext* ctx;
        JSValue proto;
        JSClassID class_id;
        std::string name;

    public:
        ClassBinder(JSContext* c, JSValue p, JSClassID id, std::string_view n)
                : ctx(c), proto(p), class_id(id), name(n) {}

        // Non-const version
        template <typename R, typename... Args>
        ClassBinder& method(std::string_view name, R (T::*func)(Args...)) {
            return method_impl<decltype(func), R, Args...>(name, func);
        }

        // Const version
        template <typename R, typename... Args>
        ClassBinder& method(std::string_view name, R (T::*func)(Args...) const) {
            return method_impl<decltype(func), R, Args...>(name, func);
        }

        template <typename V>
        ClassBinder& field(std::string_view field_name, V T::*member) {
            // 1. Store the member pointer and ClassID in a heap-allocated struct
            struct FieldAccessor {
                V T::*ptr;
                JSClassID id;
            };
            auto* acc = new FieldAccessor{ member, class_id };

            // 2. THE GETTER
            auto getter_wrap = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
                int64_t ptr_addr;
                JS_ToInt64(ctx, &ptr_addr, data[0]); // Extract the FieldAccessor pointer
                auto* acc = reinterpret_cast<FieldAccessor*>(ptr_addr);

                T* instance = static_cast<T*>(JS_GetOpaque(this_val, acc->id));
                if (!instance) return JS_ThrowTypeError(ctx, "Invalid 'this' for field getter");

                return converter<V>::put(ctx, instance->*(acc->ptr));
            };

            // 3. THE SETTER
            auto setter_wrap = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
                int64_t ptr_addr;
                JS_ToInt64(ctx, &ptr_addr, data[0]);
                auto* acc = reinterpret_cast<FieldAccessor*>(ptr_addr);

                T* instance = static_cast<T*>(JS_GetOpaque(this_val, acc->id));
                if (!instance) return JS_ThrowTypeError(ctx, "Invalid 'this' for field setter");

                // argv[0] is the value being assigned (e.g., ship.fuel = 100)
                instance->*(acc->ptr) = converter<V>::get(ctx, argv[0]);
                return JS_UNDEFINED;
            };

            // 4. Create the JS property
            JSValue data_val = JS_NewInt64(ctx, reinterpret_cast<int64_t>(acc));

            // QuickJS Getters take 0 args, Setters take 1
            JSValue js_get = JS_NewCFunctionData(ctx, getter_wrap, 0, 0, 1, &data_val);
            JSValue js_set = JS_NewCFunctionData(ctx, setter_wrap, 1, 0, 1, &data_val);

            JSAtom atom = JS_NewAtom(ctx, field_name.data());
            JS_DefinePropertyGetSet(ctx, proto, atom, js_get, js_set, JS_PROP_C_W_E);

            JS_FreeAtom(ctx, atom);
            JS_FreeValue(ctx, data_val); // We've copied the pointer into the Function Data

            return *this;
        }

        template <typename... Args>
        ClassBinder& constructor() {
            JSValue data = JS_NewInt64(ctx, static_cast<int64_t>(class_id));

            auto ctor_trampoline = [](JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
                // ... (your existing ctor_trampoline code) ...

                // 1. IMPORTANT: Check if we are being called as a constructor
                if (JS_IsUndefined(new_target)) {
                    return JS_ThrowTypeError(ctx, "Class constructor cannot be invoked without 'new'");
                }

                int64_t id_val;
                JS_ToInt64(ctx, &id_val, data[0]);
                JSClassID id = static_cast<JSClassID>(id_val);

                T* instance = ctor_helper<T, Args...>(ctx, argv, std::make_index_sequence<sizeof...(Args)>{});

                // 2. Create the object using the prototype of the new_target
                // This ensures inheritance works correctly in JS
                JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
                JSValue obj = JS_NewObjectClass(ctx, id);
                JS_SetPrototype(ctx, obj, proto);
                JS_FreeValue(ctx, proto);

                JS_SetOpaque(obj, instance);
                return obj;
            };

            // 3. Register as a Function Data
            // Note: The 'length' (argc) is sizeof...(Args)
            JSValue ctor_func = JS_NewCFunctionData(ctx, ctor_trampoline, sizeof...(Args), 0, 1, &data);

            // 4. SET THE CONSTRUCTOR FLAG
            // This is the missing piece!
            JS_SetConstructorBit(ctx, ctor_func, true);

            JSValue global = JS_GetGlobalObject(ctx);
            JS_SetPropertyStr(ctx, global, name.c_str(), ctor_func);

            JS_FreeValue(ctx, global);
            JS_FreeValue(ctx, data);
            return *this;
        }

    private:

        template <typename M, typename R, typename... Args>
        ClassBinder& method_impl(std::string_view method_name, M func) {
            // 1. Create a signature-erased wrapper.
            // We capture 'func' by value so it stays alive within the std::function.
            using WrapperType = std::function<R(T*, Args...)>;
            WrapperType* wrapper_ptr = new WrapperType([func](T* instance, Args... args) -> R {
                return (instance->*func)(std::forward<Args>(args)...);
            });

            // 2. The Method Trampoline
            // This is the C-style bridge QuickJS calls.
            auto method_trampoline = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
                // Extract the function pointer (Smuggled as int64)
                int64_t f_addr;
                JS_ToInt64(ctx, &f_addr, data[0]);
                auto* f = reinterpret_cast<WrapperType*>(f_addr);

                // Extract the Class ID
                int64_t id_val;
                JS_ToInt64(ctx, &id_val, data[1]);
                JSClassID id = static_cast<JSClassID>(id_val);

                // Get the C++ 'this' instance from the JS object
                T* instance = static_cast<T*>(JS_GetOpaque(this_val, id));

                if (!instance) {
                    return JS_ThrowTypeError(ctx, "Method called on incompatible object or null instance");
                }

                if (argc < sizeof...(Args)) {
                    return JS_ThrowTypeError(ctx, "Argument mismatch: expected %zu", sizeof...(Args));
                }

                // 3. Invoke and convert return value
                if constexpr (std::is_void_v<R>) {
                    invoke_method_helper(ctx, instance, *f, argv, std::make_index_sequence<sizeof...(Args)>{});
                    return JS_UNDEFINED;
                } else {
                    return converter<R>::put(ctx, invoke_method_helper(ctx, instance, *f, argv, std::make_index_sequence<sizeof...(Args)>{}));
                }
            };

            // 4. Bundle data for the trampoline
            JSValue data_array[2];
            data_array[0] = JS_NewInt64(ctx, reinterpret_cast<int64_t>(wrapper_ptr));
            data_array[1] = JS_NewInt64(ctx, static_cast<int64_t>(class_id));

            // 5. Create the JS Function and attach to prototype
            // We provide a "finalizer" for the CFunctionData if your QuickJS version supports it,
            // otherwise, we'd store wrapper_ptr in the Engine's cleanup vector.
            JSValue js_method = JS_NewCFunctionData(ctx, method_trampoline, sizeof...(Args), 0, 2, data_array);
            JS_SetPropertyStr(ctx, proto, method_name.data(), js_method);

            // Cleanup temporary JS handles
            JS_FreeValue(ctx, data_array[0]);
            JS_FreeValue(ctx, data_array[1]);

            return *this;
        }

        template <typename R, typename... Args, size_t... I>
        static R invoke_method_helper(JSContext* ctx, T* instance, std::function<R(T*, Args...)>& f,
                                  JSValueConst* argv, std::index_sequence<I...>) {
            return f(instance, converter<std::decay_t<Args>>::get(ctx, argv[I])...);
        }

        template <typename R, typename... Args, size_t... I>
        static auto invoke_helper(JSContext* ctx, T* instance, std::function<R(T*, Args...)>& f,
                          JSValueConst* argv, std::index_sequence<I...>) {
            return f(instance, converter<std::decay_t<Args>>::get(ctx, argv[I])...);
        }

        template <typename T, typename... Args, size_t... I>
        static T* ctor_helper(JSContext* ctx, JSValueConst* argv, std::index_sequence<I...>) {
            return new T(converter<std::decay_t<Args>>::get(ctx, argv[I])...);
        }
    };

    // --- The Engine ---
    class Engine {
    public:
        Engine() : rt(JS_NewRuntime()), ctx(JS_NewContext(rt.get())) {
            global_obj = JS_GetGlobalObject(ctx.get());
        }

        ~Engine() {
            JS_FreeValue(ctx.get(), global_obj);
        }

        std::expected<std::string, std::string> eval(std::string_view code, std::string_view filename = "input.js") {
            // JS_Eval takes a null-terminated string for the filename
            JSValue val = JS_Eval(ctx.get(), code.data(), code.size(), filename.data(), JS_EVAL_TYPE_GLOBAL);

            if (JS_IsException(val)) {
                JSValue exception = JS_GetException(ctx.get());
                std::string err_msg = converter<std::string>::get(ctx.get(), exception);
                JS_FreeValue(ctx.get(), exception);
                return std::unexpected(err_msg);
            }

            std::string result = converter<std::string>::get(ctx.get(), val);
            JS_FreeValue(ctx.get(), val);
            return result;
        }

        std::expected<std::string, std::string> run_file(const std::filesystem::path& path) {
            std::ifstream file(path);

            if (!file.is_open()) {
                return std::unexpected("Could not open file: " + path.string());
            }

            // Efficiently read the entire file into a string
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string code = buffer.str();

            // Use the filename as the 'source' for better error reporting in JS
            return eval(code, path.filename().string());
        }

        template<typename Func>
        void register_function(std::string_view name, Func&& func) {
            // We use a helper to deduce the function signature from the lambda's operator()
            register_function_impl(name, std::function(std::forward<Func>(func)));
        }

        template <typename T>
        auto register_class(std::string_view name) {
            std::type_index type_idx(typeid(T));

            // 1. Create a unique ID for this C++ Type
            JSClassID id = 0;
            JS_NewClassID(rt.get(), &id);
            class_ids[type_idx] = id;

            // 2. Define the Finalizer (The C++ 'delete' bridge)
            JSClassDef class_def = {
                name.data(),
                [](JSRuntime* rt, JSValue val) {
                    // Pull the C++ instance out and delete it
                    T* ptr = static_cast<T*>(JS_GetOpaque(val, 0));
                    if (ptr) delete ptr;
                }
            };
            JS_NewClass(rt.get(), id, &class_def);

            // 3. Create the Prototype (The 'blueprint' for instances)
            JSValue proto = JS_NewObject(ctx.get());
            JS_SetClassProto(ctx.get(), id, proto);

            // 4. Return the binder to allow .constructor() and .method() calls
            // We pass the engine's context and the class name
            return ClassBinder<T>(ctx.get(), proto, id, name);
        }

    private:
        // Helper to perform the actual call
        template<typename R, typename... Args, size_t... I>
        static JSValue invoke_helper(JSContext* ctx, std::function<R(Args...)>& f, JSValueConst* argv, std::index_sequence<I...>) {
            if constexpr (std::is_void_v<R>) {
                f(converter<std::decay_t<Args>>::get(ctx, argv[I])...);
                return JS_UNDEFINED;
            } else {
                return converter<R>::put(ctx, f(converter<std::decay_t<Args>>::get(ctx, argv[I])...));
            }
        }

        template<typename R, typename... Args>
        void register_function_impl(std::string_view name, std::function<R(Args...)> func) {
            // 1. Wrap the function in std::any and put it in a unique_ptr.
            // unique_ptr ensures the memory address of the function is stable
            // even if the vector 'functions_' grows and reallocates.
            auto storage = std::make_unique<std::any>(std::make_any<std::function<R(Args...)>>(std::move(func)));

            // Get the raw pointer to the std::function inside the std::any
            auto* f_ptr = std::any_cast<std::function<R(Args...)>>(storage.get());

            // Store the unique_ptr in our vector to keep it alive
            functions.push_back(std::move(storage));

            // 2. Pass the address of the function to JS as a 64-bit integer
            JSValue data = JS_NewInt64(ctx.get(), reinterpret_cast<int64_t>(f_ptr));

            // 3. The Trampoline (C-style callback)
            auto trampoline = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
                int64_t ptr_addr;
                JS_ToInt64(ctx, &ptr_addr, data[0]);

                // Cast the memory address back to our function type
                auto* f = reinterpret_cast<std::function<R(Args...)>*>(ptr_addr);

                if (argc < sizeof...(Args)) {
                    return JS_ThrowTypeError(ctx, "Expected %zu arguments, got %d", sizeof...(Args), argc);
                }

                return invoke_helper(ctx, *f, argv, std::make_index_sequence<sizeof...(Args)>{});
            };

            // 4. Register with QuickJS
            const JSValue js_func = JS_NewCFunctionData(ctx.get(), trampoline, sizeof...(Args), 0, 1, &data);
            JS_SetPropertyStr(ctx.get(), global_obj, name.data(), js_func);

            // Clean up the temporary JS wrapper for the pointer
            JS_FreeValue(ctx.get(), data);
        }

        struct RuntimeDeleter { void operator()(JSRuntime* rt) const { JS_FreeRuntime(rt); } };
        struct ContextDeleter { void operator()(JSContext* ctx) const { JS_FreeContext(ctx); } };

        std::unique_ptr<JSRuntime, RuntimeDeleter> rt;
        std::unique_ptr<JSContext, ContextDeleter> ctx;
        JSValue global_obj{};
        std::vector<std::unique_ptr<std::any>> functions;
        std::map<std::type_index, JSClassID> class_ids;
    };

}