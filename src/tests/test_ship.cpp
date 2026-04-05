#include "../lib/qjswrapper.hpp"
#include "../js/ship.h"
#include <iostream>

class Ship {
public:
    Ship(std::string name, int fuel) : name(name), fuel(fuel) {}
    void fly(int dist) { if (fuel >= dist) fuel -= dist; }
    int get_fuel() const { return fuel; }
    std::string get_name() const { return name; }
    std::string name;
    int fuel;
};

int main() {
    qjs::Engine engine;
    auto global = engine.get_global_object();

    global.register_class<Ship>("Ship")
        .constructor<std::string, int>()
        .method("fly", &Ship::fly)
        .method("refuel", [](Ship* s, int a) { s->fuel += a; })
        .method("getFuel", &Ship::get_fuel)
        .method("getName", &Ship::get_name);

    // Run the main logic from bytecode
    if (auto result = engine.run_bytecode(qjsc_ship, qjsc_ship_size); !result) {
        std::cerr << "Bytecode Error: " << result.error() << std::endl;
        return 1;
    }

    // Validation: Check state using globalThis.voyager
    auto fuel_check = engine.eval_global("globalThis.voyager.getFuel()", "verify.js");
    if (!fuel_check || *fuel_check != "20") {
        std::cerr << "Logic Error: Expected 20 fuel, got " << (fuel_check ? *fuel_check : "ERR") << std::endl;
        return 1;
    }

    std::cout << "Ship integration tests passed." << std::endl;
    return 0;
}