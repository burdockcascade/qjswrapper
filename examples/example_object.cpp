#include <print>
#include "../include/qjswrap.hpp"

int main() {
    qjs::Engine engine;
    qjs::Value global = engine.global();

    // ==========================================
    // 1. Creating a New Object in C++
    // ==========================================

    // Create the main object
    qjs::Value player = engine.make_object()
        .set_variable("name", "Hero")
        .set_variable("level", 1)
        .set_constant("id", 9999);

    // Create a nested object for coordinates
    qjs::Value position = engine.make_object()
        .set_variable("x", 10.5)
        .set_variable("y", 20.0);

    // Attach the nested object to the player
    player.set_variable("pos", position);

    // Inject the fully built object into the global JS environment
    global.set_variable("Player1", player);

    // ==========================================
    // 2. Modifying it inside JavaScript
    // ==========================================

   std::ignore = engine.eval(R"(
        // JS can read and modify the C++ created object
        Player1.level += 1;
        Player1.pos.x += 5.0;
        Player1.status = "Poisoned"; // JS can add new properties too!
    )");

    // ==========================================
    // 3. Extracting the Object back to C++
    // ==========================================

    // Fetch the modified object
    qjs::Value updated_player = global.get_property("Player1");

    if (updated_player.is_object()) {
        // Read the new status added by JS
        if (const auto status = updated_player.get_property("status").as<std::string>()) {
            std::println("Player Status: {}", *status);
        }

        // Read the nested position object
        qjs::Value updated_pos = updated_player.get_property("pos");
        if (updated_pos.is_object()) {
            if (auto x = updated_pos.get_property("x").as<double>()) {
                std::println("New X Position: {}", *x);
            }
        }
    }

    return 0;
}