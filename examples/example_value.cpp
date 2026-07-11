#include <iostream>
#include "../include/qjswrapper.hpp"

int main() {
    qjs::Engine engine;
    qjs::Value global = engine.global();

    // 1. Setup environment
    std::ignore = engine.eval(R"(
        function greet_user(name, age) {
            return "Hello " + name + ", you are " + age + " years old!";
        }
    )");

    // 2. Fetch the function
    qjs::Value greet_func = global.get_property("greet_user");

    // 3. Call the function using our ultra-clean high-level factories
    if (greet_func.is_function()) {
        const qjs::Value result = greet_func.call({
            engine.make_value("Alice"),
            engine.make_value(30)
        });

        if (const auto str_res = result.as<std::string>()) {
            std::cout << *str_res << "\n";
            // Outputs: Hello Alice, you are 30 years old!
        }
    }

    // 4. Build a JS Object entirely in C++
    qjs::Value my_config = engine.make_object();
    my_config.set_variable("resolution", "1080p");
    my_config.set_variable("fps", 60);
    my_config.set_variable("fullscreen", true);

    // Inject it into JS
    global.set_variable("AppConfig", my_config);

    return 0; // Engine and all Values safely destroyed
}