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
    global.register_function("sub", cpp_sub);
    
    auto res = engine.eval_global("add(5, 10)", "test.js");
    if (res) std::cout << "Result: " << *res << std::endl;
    return 0;
}