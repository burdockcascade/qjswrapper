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
#include "quickjs.h"

namespace qjs {

    template<typename T>
    struct converter {

        // --- FROM JS (Get from JS) ---
        static T get(JSContext* ctx, JSValueConst v) {
            if constexpr (std::integral<T>) {
                if constexpr (std::is_same_v<T, bool>) {
                    return JS_ToBool(ctx, v);
                } else {
                    int32_t val = 0;
                    JS_ToInt32(ctx, &val, v);
                    return static_cast<T>(val);
                }
            }
            else if constexpr (std::floating_point<T>) {
                double val = 0;
                JS_ToFloat64(ctx, &val, v);
                return static_cast<T>(val);
            }
            else if constexpr (std::is_convertible_v<T, std::string>) {
                size_t len;
                const char* str = JS_ToCStringLen(ctx, &len, v);
                std::string s(str ? str : "", len);
                JS_FreeCString(ctx, str);
                return s;
            }
            else {
                static_assert(sizeof(T) == 0, "Unsupported type for QJS conversion");
            }
            return T{};
        }

        // --- TO JS (Put into JS) ---
        static JSValue put(JSContext* ctx, const T& val) {
            if constexpr (std::integral<T>) {
                if constexpr (std::is_same_v<T, bool>) {
                    return JS_NewBool(ctx, val);
                } else {
                    return JS_NewInt32(ctx, static_cast<int32_t>(val));
                }
            }
            else if constexpr (std::floating_point<T>) {
                return JS_NewFloat64(ctx, static_cast<double>(val));
            }
            else if constexpr (std::is_convertible_v<T, std::string_view>) {
                return JS_NewStringLen(ctx, val.data(), val.size());
            }
            else {
                return JS_UNDEFINED;
            }
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
    };

}