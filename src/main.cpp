#include <any>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <expected>
#include "quickjs.h"

namespace qjs {

    // --- Type Converters ---
    template<typename T> struct converter;

    template<> struct converter<int> {
        static int get(JSContext* ctx, JSValueConst v) {
            int val = 0;
            JS_ToInt32(ctx, &val, v);
            return val;
        }
        static JSValue put(JSContext* ctx, int val) { return JS_NewInt32(ctx, val); }
    };

    template<> struct converter<std::string> {
        static std::string get(JSContext* ctx, JSValueConst v) {
            size_t len;
            const char* str = JS_ToCStringLen(ctx, &len, v);
            std::string s(str ? str : "", len);
            JS_FreeCString(ctx, str);
            return s;
        }
        static JSValue put(JSContext* ctx, const std::string& val) {
            return JS_NewStringLen(ctx, val.data(), val.size());
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
    qjs::Engine engine;

    // Bind a native C++ lambda with complex types
    engine.bind("add", [](int a, int b) {
        return a + b;
    });

    engine.bind("add2", cpp_add);

    engine.bind("greet", [](const std::string& name) {
        return "Hello from C++, " + name + "!";
    });

    const auto res1 = engine.eval("add2(10, 32)");
    const auto res2 = engine.eval("greet('Gemini')");

    if (res1) {
        std::cout << "Result 1: " << *res1 << "\n"; // 42
    } else {
        std::cout << "Error: " << *res1 << "\n";
    }
    if (res2) std::cout << "Result 2: " << *res2 << "\n"; // Hello from C++, Gemini!

    return 0;
}