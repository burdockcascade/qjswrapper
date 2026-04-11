#include <iostream>
#include <string>
#include "../include/qjswrapper.hpp"

int main() {
    qjs::Engine engine;
    auto config_obj = engine.make_object();

    // Set a normal property and a read-only property
    config_obj
        .set("theme", "dark")
        .set("version", "1.0.4", qjs::Prop::ReadOnly);

    engine.global().set("config", config_obj);

    // Let's try to overwrite them in JavaScript
    auto result = engine.eval(R"(
        "use strict"; // Strict mode forces read-only assignments to throw an error

        config.theme = "light"; // This works fine

        try {
            config.version = "2.0.0"; // This will throw an error!
        } catch (e) {
            // e is TypeError: "version" is read-only
        }

        config.version; // Return the version to C++ to prove it hasn't changed
    )", "readonly_test.js");

    std::cout << "Final theme: " << config_obj.get<std::string>("theme") << "\n";
    std::cout << "Final version: " << result.value() << "\n";
    // Output will show theme is "light", but version remains "1.0.4"

    return 0;
}