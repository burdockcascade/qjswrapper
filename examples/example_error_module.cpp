#include <iostream>
#include <string>
#include <print>
#include "../include/qjswrapper.hpp"

int main() {
    qjs::Engine engine;


    const std::string js_code = R"(
        console.log("")
    )";

    std::cout << "Executing JS script...\n";
    auto result = engine.eval(js_code, "demo.js", qjs::EvalMode::Module);

    if (result.has_value()) {
        std::println("\n[C++]: Script evaluated successfully.");
        std::println("[C++]: Eval returned: {}", result.value());
    } else {
        const auto& err = result.error();
        std::println("\n[C++]: Script evaluation failed.");
        std::println(stderr, "{}", err.to_string());
        return 1;
    }

    return 0;
}