#include "../lib/qjswrapper.hpp"
#include <iostream>

void add_console_utilities(qjs::Engine& engine) {
    auto global = engine.get_global_object();
    auto console = engine.create_object("console");

    console.register_function("log", [](std::string msg) {
        std::cout << "[JS] " << msg << std::endl;
    });
}

int cpp_sub(const int a, const int b) {
    return a - b;
}

int main() {
    qjs::Engine engine;
    add_console_utilities(engine);

    auto global = engine.get_global_object();
    global.register_function("add", [](int a, int b) { return a + b; });
    
    auto res = engine.eval_global("add(5, 10)", "test.js");
    
    if (!res) {
        std::cerr << "Execution failed: " << res.error() << std::endl;
        return 1; // Failure
    }

    if (*res != "15") {
        std::cerr << "Assertion failed! Expected 15, got " << *res << std::endl;
        return 1; // Failure
    }

    std::cout << "Math test passed!" << std::endl;
    return 0; // Success
}