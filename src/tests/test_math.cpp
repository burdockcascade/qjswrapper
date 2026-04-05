#include "../lib/qjswrapper.hpp"
#include <iostream>
#include <vector>

int main() {
    qjs::Engine engine;
    auto global = engine.get_global_object();

    // Register test functions
    global.register_function("add", [](int a, int b) { return a + b; });
    global.register_function("sub", [](int a, int b) { return a - b; });

    // Test cases: [Expression, Expected Result]
    std::vector<std::pair<std::string, std::string>> test_cases = {
        {"add(10, 5)", "15"},
        {"sub(10, 5)", "5"},
        {"add(-1, 1)", "0"}
    };

    for (const auto& [expr, expected] : test_cases) {
        auto res = engine.eval_global(expr, "test.js");
        if (!res) {
            std::cerr << "Execution Error: " << res.error() << std::endl;
            return 1;
        }
        if (*res != expected) {
            std::cerr << "Assertion Failed! " << expr << " expected " << expected << " but got " << *res << std::endl;
            return 1;
        }
    }

    std::cout << "Math tests passed successfully." << std::endl;
    return 0;
}