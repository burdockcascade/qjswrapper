#include <catch2/catch_test_macros.hpp>
#include <string>
#include "../main/qjswrapper.hpp"

// A dummy C++ class to act as our test subject
class DummyActor {
public:
    DummyActor(std::string name, int hp) : name_(std::move(name)), hp_(hp) {}

    void take_damage(const int dmg) { hp_ -= dmg; }
    [[nodiscard]] int get_hp() const { return hp_; }
    [[nodiscard]] std::string get_name() const { return name_; }

    static std::string get_type() { return "Actor"; }

private:
    std::string name_;
    int hp_;
};

TEST_CASE("Class Builder bindings", "[class]") {
    qjs::Engine engine;

    // 1. Define the class and bind all property types
    auto actor_class = engine.define_class<DummyActor>("DummyActor");

    actor_class
        .constructor<std::string, int>()
        .method("takeDamage", &DummyActor::take_damage)
        .method("getHp", &DummyActor::get_hp)
        .method("getName", &DummyActor::get_name)
        .static_method("getType", &DummyActor::get_type)
        .variable("defaultStance", "neutral")
        .constant("MAX_LEVEL", 100)
        .static_variable("globalCount", 0)
        .static_constant("VERSION", 1);

    SECTION("Constructor and Instance Methods") {
        auto result = engine.eval(R"(
            const a = new DummyActor("Hero", 50);
            a.takeDamage(10);
            a.getHp() === 40 && a.getName() === "Hero";
        )", "test_methods.js");

        REQUIRE(result.has_value());
        REQUIRE(result.value() == "true");
    }

    SECTION("Static Methods") {
        auto result = engine.eval(R"(
            DummyActor.getType() === "Actor";
        )", "test_static_methods.js");

        REQUIRE(result.has_value());
        REQUIRE(result.value() == "true");
    }

    SECTION("Prototype Variables (Mutable defaults)") {
        auto result = engine.eval(R"(
            const a1 = new DummyActor("A", 10);
            const a2 = new DummyActor("B", 10);

            let check1 = (a1.defaultStance === "neutral");

            // Shadowing the prototype variable on a1
            a1.defaultStance = "aggressive";

            let check2 = (a1.defaultStance === "aggressive" && a2.defaultStance === "neutral");
            check1 && check2;
        )", "test_variables.js");

        REQUIRE(result.has_value());
        REQUIRE(result.value() == "true");
    }

    SECTION("Prototype Constants (Read-only)") {
        auto result = engine.eval(R"(
            const a = new DummyActor("A", 10);
            let check1 = (a.MAX_LEVEL === 100);

            // Attempting to overwrite a constant should fail (silently in non-strict mode)
            a.MAX_LEVEL = 999;

            let check2 = (a.MAX_LEVEL === 100);
            check1 && check2;
        )", "test_constants.js");

        REQUIRE(result.has_value());
        REQUIRE(result.value() == "true");
    }

    SECTION("Static Variables (Mutable)") {
        auto result = engine.eval(R"(
            let check1 = (DummyActor.globalCount === 0);

            DummyActor.globalCount = 5;

            let check2 = (DummyActor.globalCount === 5);
            check1 && check2;
        )", "test_static_vars.js");

        REQUIRE(result.has_value());
        REQUIRE(result.value() == "true");
    }

    SECTION("Static Constants (Read-only)") {
        auto result = engine.eval(R"(
            let check1 = (DummyActor.VERSION === 1);

            // Attempting to overwrite a static constant should fail
            DummyActor.VERSION = 2;

            let check2 = (DummyActor.VERSION === 1);
            check1 && check2;
        )", "test_static_consts.js");

        REQUIRE(result.has_value());
        REQUIRE(result.value() == "true");
    }
}