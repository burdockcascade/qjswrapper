#include "../lib/qjswrapper.hpp"
#include <iostream>

int main() {
    qjs::Engine engine;

    // Test 1: Syntax Error
    auto syntax_res = engine.eval_global("const x = ;", "syntax_error.js");
    if (syntax_res) {
        std::cerr << "Failed: Should have caught syntax error" << std::endl;
        return 1;
    }
    std::cout << "Caught expected syntax error: " << syntax_res.error() << std::endl;

    // Test 2: Runtime Error (Calling non-existent function)
    auto runtime_res = engine.eval_global("nonExistentFunction()", "runtime_error.js");
    if (runtime_res) {
        std::cerr << "Failed: Should have caught runtime error" << std::endl;
        return 1;
    }
    std::cout << "Caught expected runtime error: " << runtime_res.error() << std::endl;

    return 0;
}