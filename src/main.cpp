#include <iostream>
#include <string>
#include <string_view>
#include <memory>
#include <expected>
#include <functional>
#include <type_traits>
#include "quickjs.h"

namespace qjs {

    template<typename T>
    struct converter;

    template<>
    struct converter<int> {
        static int get(JSContext* ctx, JSValueConst v) {
            int val;
            JS_ToInt32(ctx, &val, v);
            return val;
        }
        static JSValue put(JSContext* ctx, int val) {
            return JS_NewInt32(ctx, val);
        }
    };

    template<>
    struct converter<std::string> {
        static std::string get(JSContext* ctx, JSValueConst v) {
            size_t len;
            const char* str = JS_ToCStringLen(ctx, &len, v);
            std::string s(str, len);
            JS_FreeCString(ctx, str);
            return s;
        }
        static JSValue put(JSContext* ctx, const std::string& val) {
            return JS_NewStringLen(ctx, val.data(), val.size());
        }
    };

    template <typename R, typename... Args, std::size_t... I>
    static JSValue call_cpp_func(JSContext* ctx,
                                 std::function<R(Args...)>& f,
                                 int argc,
                                 JSValueConst* argv,
                                 std::index_sequence<I...>) {

        // C++23: Use the converter to transform each JSValueConst into the target Type
        if constexpr (std::is_void_v<R>) {
            f(converter<std::decay_t<Args>>::get(ctx, argv[I])...);
            return JS_UNDEFINED;
        } else {
            R result = f(converter<std::decay_t<Args>>::get(ctx, argv[I])...);
            return converter<R>::put(ctx, result);
        }
    }

    // --- RAII Wrappers for Context/Runtime ---

    struct RuntimeDeleter {
        void operator()(JSRuntime* rt) const { JS_FreeRuntime(rt); }
    };

    struct ContextDeleter {
        void operator()(JSContext* ctx) const { JS_FreeContext(ctx); }
    };

    using RuntimePtr = std::unique_ptr<JSRuntime, RuntimeDeleter>;
    using ContextPtr = std::unique_ptr<JSContext, ContextDeleter>;

    // --- Value Wrapper ---

    class Value {
    public:
        Value(JSContext* ctx, JSValue v) : ctx_(ctx), v_(v) {}

        // RAII for JSValue reference counting
        ~Value() { JS_FreeValue(ctx_, v_); }

        // Rule of 5: Copying increments ref count
        Value(const Value& other) : ctx_(other.ctx_), v_(JS_DupValue(other.ctx_, other.v_)) {}
        Value& operator=(const Value& other) {
            if (this != &other) {
                JS_FreeValue(ctx_, v_);
                ctx_ = other.ctx_;
                v_ = JS_DupValue(other.ctx_, other.v_);
            }
            return *this;
        }

        Value(Value&& other) noexcept : ctx_(other.ctx_), v_(other.v_) {
            other.v_ = JS_UNDEFINED;
        }

        [[nodiscard]] std::string to_string() const {
            size_t len;
            const char* str = JS_ToCStringLen(ctx_, &len, v_);
            if (!str) return "";
            std::string result(str, len);
            JS_FreeCString(ctx_, str);
            return result;
        }

        [[nodiscard]] bool is_exception() const { return JS_IsException(v_); }

    private:
        JSContext* ctx_;
        JSValue v_;
    };

    // --- The Engine Wrapper ---

    class Engine {
    public:
        Engine() : rt_(JS_NewRuntime()), ctx_(JS_NewContext(rt_.get())) {
            global_obj_ = JS_GetGlobalObject(ctx_.get());
        }

        ~Engine() {
            JS_FreeValue(ctx_.get(), global_obj_);
        }

        /**
         * Evaluates JavaScript code.
         * Returns a Value object or a string containing the error message.
         */
        std::expected<Value, std::string> eval(std::string_view code, std::string_view filename = "main.js") {
            JSValue val = JS_Eval(ctx_.get(), code.data(), code.size(), filename.data(), JS_EVAL_TYPE_GLOBAL);

            if (JS_IsException(val)) {
                JSValue exception = JS_GetException(ctx_.get());
                Value err_val(ctx_.get(), exception);
                return std::unexpected(err_val.to_string());
            }

            return Value(ctx_.get(), val);
        }

        void bind_function(std::string_view name, JSCFunction* func, int arg_count = 0) {
            JS_SetPropertyStr(ctx_.get(), global_obj_, name.data(),
                             JS_NewCFunction(ctx_.get(), func, name.data(), arg_count));
        }

        template <typename R, typename... Args>
        void bind(std::string_view name, std::function<R(Args...)> func) {

            // We need to keep the std::function alive.
            // In a real wrapper, you'd store this in a map inside the Engine.
            auto* func_ptr = new std::function<R(Args...)>(std::move(func));

            // Define the C callback that QuickJS understands
            auto wrapper = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv,
                              int magic, JSValue* data) -> JSValue {

                // Retrieve our stored pointer (magic/data handling simplified for this example)
                auto* f = static_cast<std::function<R(Args...)>*>(JS_GetOpaque(data[0], 0));

                // Trigger the unpacker with an index sequence (0, 1, 2...)
                return call_cpp_func(ctx, *f, argc, argv, std::make_index_sequence<sizeof...(Args)>{});
            };

            // Using JS_NewCFunctionData allows us to attach the C++ function pointer
            // to the JS function object itself so it doesn't get lost.
            JSValue js_func = JS_NewCFunctionData(ctx_.get(), wrapper, sizeof...(Args), 0, 1, nullptr);
            JS_SetPropertyStr(ctx_.get(), global_obj_, name.data(), js_func);
        }

    private:
        RuntimePtr rt_;
        ContextPtr ctx_;
        JSValue global_obj_{};
    };
}

// --- Usage Example ---

JSValue cpp_add(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 2) return JS_EXCEPTION;

    int32_t a, b;
    if (JS_ToInt32(ctx, &a, argv[0]) || JS_ToInt32(ctx, &b, argv[1])) {
        return JS_EXCEPTION;
    }

    return JS_NewInt32(ctx, a + b);
}

// --- Usage ---

int main() {
    qjs::Engine engine;

    // Bind our C++ function to the name "add" in JS
    engine.bind_function("add", cpp_add, 2);

    // Call the C++ function from within a JS string
    auto result = engine.eval("const sum = add(15, 27); `The sum is ${sum}`");

    if (result) {
        std::cout << "JS Output: " << result->to_string() << std::endl;
        // Output: JS Output: The sum is 42
    } else {
        std::cerr << "Error: " << result.error() << std::endl;
    }

    return 0;
}