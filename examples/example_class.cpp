#include <iostream>
#include <string>
#include "../src/qjswrapper.hpp"

class Player {
public:
    Player(std::string name, int health)
        : name_(std::move(name)), health_(health) {}

    void heal(int amount) {
        health_ += amount;
        std::cout << "[C++] " << name_ << " healed by " << amount
                  << ". New health: " << health_ << "\n";
    }

    [[nodiscard]] int get_health() const { return health_; }
    [[nodiscard]] std::string get_name() const { return name_; }

private:
    std::string name_;
    int health_;
};

int main() {
    qjs::Engine engine;

    // 1. Create the class shell
    auto player_class = engine.define_class<Player>("Player");

    // 2. Register Constructors
    player_class
        .constructor<std::string, int>() // Standard 2-arg constructor
        .constructor([](std::string name) { // Custom 1-arg constructor (Default health)
            std::cout << "[C++] Constructing Player using custom factory!\n";
            return Player(name, 100);
        })
        .method("heal", &Player::heal)
        .method("getHealth", &Player::get_health)
        .method("getName", &Player::get_name)
        .constant("CLASS_TYPE", "Warrior")
        .static_constant("MAX_HEALTH", 100);

    // 3. Test overloading in JavaScript
    std::string js_code = R"(
        // Uses the standard constructor
        const hero1 = new Player("Arthur", 80);
        console.log("Hero 1: " + hero1.getName() + " - " + hero1.getHealth());

        // Uses the custom factory constructor!
        const hero2 = new Player("Galahad");
        console.log("Hero 2: " + hero2.getName() + " - " + hero2.getHealth());

        hero1.heal(25);
    )";

    engine.global().set("console", engine.make_object().set("log", [](std::string msg) {
        std::cout << "[JS] " << msg << "\n";
    }));

    std::cout << "--- Running Class Overload Demo ---\n";
    auto result = engine.eval(js_code, "main.js");

    if (!result) {
        std::cerr << "Error: " << result.error() << "\n";
    }

    return 0;
}