#include <any>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <expected>
#include "quickjs.h"
#include "raylib.h"


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
            // Raylib specific branch
            else if constexpr (std::is_same_v<T, Color>) {
                uint32_t r, g, b, a;
                JSValue r_val = JS_GetPropertyStr(ctx, v, "r");
                JSValue g_val = JS_GetPropertyStr(ctx, v, "g");
                JSValue b_val = JS_GetPropertyStr(ctx, v, "b");
                JSValue a_val = JS_GetPropertyStr(ctx, v, "a");

                JS_ToUint32(ctx, &r, r_val);
                JS_ToUint32(ctx, &g, g_val);
                JS_ToUint32(ctx, &b, b_val);
                JS_ToUint32(ctx, &a, a_val);

                JS_FreeValue(ctx, r_val); JS_FreeValue(ctx, g_val);
                JS_FreeValue(ctx, b_val); JS_FreeValue(ctx, a_val);

                return Color{static_cast<unsigned char>(r), static_cast<unsigned char>(g), static_cast<unsigned char>(b), static_cast<unsigned char>(a)};
            }
            else if constexpr (std::is_same_v<T, Vector2>) {
                double x, y;
                JSValue x_val = JS_GetPropertyStr(ctx, v, "x");
                JSValue y_val = JS_GetPropertyStr(ctx, v, "y");
                JS_ToFloat64(ctx, &x, x_val);
                JS_ToFloat64(ctx, &y, y_val);
                JS_FreeValue(ctx, x_val); JS_FreeValue(ctx, y_val);
                return Vector2{static_cast<float>(x), static_cast<float>(y)};
            } else {
                static_assert(sizeof(T) == 0, "Unsupported type for QJS conversion");
            }
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
            // Raylib specific branch
            else if constexpr (std::is_same_v<T, Color>) {
                JSValue obj = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, obj, "r", JS_NewInt32(ctx, val.r));
                JS_SetPropertyStr(ctx, obj, "g", JS_NewInt32(ctx, val.g));
                JS_SetPropertyStr(ctx, obj, "b", JS_NewInt32(ctx, val.b));
                JS_SetPropertyStr(ctx, obj, "a", JS_NewInt32(ctx, val.a));
                return obj;
            }
            else if constexpr (std::is_same_v<T, Vector2>) {
                JSValue obj = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, val.x));
                JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, val.y));
                return obj;
            }
            else {
                return JS_UNDEFINED;
            }
        }
    };

    // --- The Engine ---
    class Engine {
    public:
        Engine() : rt_(JS_NewRuntime()), ctx_(JS_NewContext(rt_.get())) {
            global_obj_ = JS_GetGlobalObject(ctx_.get());
        }

        ~Engine() {
            JS_FreeValue(ctx_.get(), global_obj_);
        }

        std::expected<std::string, std::string> eval(std::string_view code) {
            const JSValue val = JS_Eval(ctx_.get(), code.data(), code.size(), "input.js", JS_EVAL_TYPE_GLOBAL);
            if (JS_IsException(val)) {
                const JSValue exception = JS_GetException(ctx_.get());
                std::string err = converter<std::string>::get(ctx_.get(), exception);
                JS_FreeValue(ctx_.get(), exception);
                return std::unexpected(err);
            }
            std::string res = converter<std::string>::get(ctx_.get(), val);
            JS_FreeValue(ctx_.get(), val);
            return res;
        }

        template<typename Func>
        void bind(std::string_view name, Func&& func) {
            // We use a helper to deduce the function signature from the lambda's operator()
            bind_impl(name, std::function(std::forward<Func>(func)));
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
        void bind_impl(std::string_view name, std::function<R(Args...)> func) {
            // 1. Wrap the function in std::any and put it in a unique_ptr.
            // unique_ptr ensures the memory address of the function is stable
            // even if the vector 'functions_' grows and reallocates.
            auto storage = std::make_unique<std::any>(std::make_any<std::function<R(Args...)>>(std::move(func)));

            // Get the raw pointer to the std::function inside the std::any
            auto* f_ptr = std::any_cast<std::function<R(Args...)>>(storage.get());

            // Store the unique_ptr in our vector to keep it alive
            functions_.push_back(std::move(storage));

            // 2. Pass the address of the function to JS as a 64-bit integer
            JSValue data = JS_NewInt64(ctx_.get(), reinterpret_cast<int64_t>(f_ptr));

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
            JSValue js_func = JS_NewCFunctionData(ctx_.get(), trampoline, sizeof...(Args), 0, 1, &data);
            JS_SetPropertyStr(ctx_.get(), global_obj_, name.data(), js_func);

            // Clean up the temporary JS wrapper for the pointer
            JS_FreeValue(ctx_.get(), data);
        }

        struct RuntimeDeleter { void operator()(JSRuntime* rt) const { JS_FreeRuntime(rt); } };
        struct ContextDeleter { void operator()(JSContext* ctx) const { JS_FreeContext(ctx); } };

        std::unique_ptr<JSRuntime, RuntimeDeleter> rt_;
        std::unique_ptr<JSContext, ContextDeleter> ctx_;
        JSValue global_obj_{};
        std::vector<std::unique_ptr<std::any>> functions_;
    };

}

int cpp_add(int a, int b) {
    return a + b;
}

// --- Usage ---
int main() {
    InitWindow(800, 450, "Raylib + QuickJS");
    qjs::Engine engine;

    // Bind DrawCircle(int centerX, int centerY, float radius, Color color)
    engine.bind("drawCircle", [](int x, int y, float radius, Color color) {
        DrawCircle(x, y, radius, color);
    });

    // Bind ClearBackground(Color color)
    engine.bind("clearBackground", [](Color color) {
        ClearBackground(color);
    });

    while (!WindowShouldClose()) {
        BeginDrawing();

        // You could run a script here, or better yet, call a JS 'update' function
        engine.eval("clearBackground({r: 20, g: 20, b: 20, a: 255});");
        engine.eval("drawCircle(400, 225, 50, {r: 255, g: 0, b: 0, a: 255});");

        EndDrawing();
    }

    CloseWindow();
    return 0;
}