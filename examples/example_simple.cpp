#include <print>
#include "../include/qjswrap.hpp"

int main() {
    qjs::Engine engine;

    // 1. Evaluate simple math
    auto res1 = engine.eval("const a = 10; const b = 20; a + b;");
    if (res1) {
        std::println("Result: {}", res1.value());
    }

    // 2. Catch syntax errors gracefully
    auto res2 = engine.eval("const broken = {");
    if (!res2) {
        std::println("Error: {}", res2.error());
    }

    // 3. Evaluate a file
    // auto file_res = engine.evalFile("script.js");

    return 0; // Engine destructor automatically frees context and runtime
}