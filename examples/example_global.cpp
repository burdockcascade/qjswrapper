#include <iostream>
#include <print>
#include "../include/qjswrap.hpp"

static JSValue nativeConsoleLog(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    for (int i = 0; i < argc; ++i) {
        if (const char *str = JS_ToCString(ctx, argv[i])) {
            std::cout << str << (i == argc - 1 ? "" : " ");
            JS_FreeCString(ctx, str);
        }
    }
    std::cout << "\n";
    return JS_UNDEFINED;
}

int main() {
    qjs::Engine engine;
    engine.global()
        .set_constant("PI", 3.14159)
        .set_constant("APP_NAME", "My Awesome Engine")
        .set_variable("score", 0)
        .set_variable("is_running", true)
        .set_cfunction("print", nativeConsoleLog, 1);

    // Test the immutability
    const std::string script = R"(
        print("Welcome to " + APP_NAME);

        score += 100; // This works fine (set_variable)
        print("Score is: " + score);

        // This will silently fail (or throw in JS strict mode) because PI is a constant
        PI = 4.0;
        print("PI is still: " + PI);
    )";

    if (auto result = engine.eval(script); !result) {
        std::println("Error: {}", result.error());
    }

    return 0;
}