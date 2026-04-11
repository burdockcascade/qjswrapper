#include <iostream>
#include <string>
#include "../src/qjswrapper.hpp"

int main() {
    std::cout << "--- QuickJS C++ Wrapper Demo ---\n\n";

    // 1. Initialize the Javascript Engine
    // This safely wraps the JSRuntime and JSContext lifecycles.
    qjs::Engine engine;

    // 2. Set Global Primitive Variables
    // We can inject configuration or state directly into the JS global object.
    engine.global()
        .set("APP_NAME", "MathMagic Pro", qjs::Prop::ReadOnly)
        .set("VERSION", 1.0, qjs::Prop::ReadOnly)
        .set("DEBUG_MODE", true);

    // 3. Bind a C++ Lambda as a Global JS Function
    // This allows JS to call back into our C++ environment (e.g., for logging).
    engine.global().set("log", [](const std::string& msg) {
        std::cout << "[JS Log]: " << msg << "\n";
    });

    // 4. The Object Builder Pattern
    // Create a standalone C++ object and chain properties/methods onto it.
    auto math_obj = engine.make_object();

    math_obj
        .set("PI", 3.14159)
        .set("add", [](double a, double b) {
            return a + b;
        })
        .set("subtract", [](double a, double b) {
            return a - b;
        })
        .set("isEven", [](int n) {
            return n % 2 == 0;
        });

    // Mount our constructed object into the JS global scope under a namespace.
    engine.global().set("MathUtils", math_obj);

    // 5. Write the JavaScript code
    // This script will consume the C++ variables, objects, and functions we just bound.
    const std::string js_code = R"(
        log("Starting " + APP_NAME + " v" + VERSION);

        // Call the C++ lambdas attached to our object
        let sum = MathUtils.add(10.5, 20.2);
        log("10.5 + 20.2 = " + sum);

        let check = MathUtils.isEven(42);
        log("Is 42 even? " + check);

        // Perform a calculation and store it in a new global JS variable
        var JS_FINAL_RESULT = MathUtils.subtract(sum, MathUtils.PI);

        // The last evaluated expression is returned to eval_global
        "Calculation complete!";
    )";

    // 6. Execute the Script
    std::cout << "Executing JS script...\n";
    auto result = engine.eval(js_code, "demo.js");

    // 7. Handle the Evaluation Result
    // Your engine returns std::expected<std::string, std::string>
    if (result.has_value()) {
        std::cout << "\n[C++]: Script evaluated successfully.\n";
        std::cout << "[C++]: Eval returned: " << result.value() << "\n";
    } else {
        std::cerr << "\n[C++ Error]: Script failed: " << result.error() << "\n";
        return 1;
    }

    // 8. Extract Data Back Natively
    // Read the variable that JavaScript created during execution directly back into C++ types.
    if (engine.global().has("JS_FINAL_RESULT")) {
        const auto native_result = engine.global().get<double>("JS_FINAL_RESULT");
        std::cout << "[C++]: Extracted JS_FINAL_RESULT natively: " << native_result << "\n";
    }

    return 0;
}