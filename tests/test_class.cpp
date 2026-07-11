#include <catch2/catch_all.hpp>
#include "../src/engine.hpp"
#include <cmath>

// A sample C++ struct to test our bindings
struct TestVector {
    float x;
    float y;

    TestVector() : x(0.0f), y(0.0f) {}
    TestVector(float x, float y) : x(x), y(y) {}

    float length() {
        return std::sqrt(x * x + y * y);
    }

    void scale(float factor) {
        x *= factor;
        y *= factor;
    }
};

struct JSColor {
private:
    int r_ = 0;
    int g_ = 0;
    int b_ = 0;

public:
    JSColor() = default;
    JSColor(int r, int g, int b) : r_(r), g_(g), b_(b) {}

    // Getters
    int getR() const { return r_; }
    int getG() const { return g_; }
    int getB() const {
        if (b_ < 0) {
            throw std::runtime_error("Corrupted blue channel state!");
        }
        return b_;
    }

    // Setters
    void setR(int r) { r_ = r; }
    void setG(int g) { g_ = g; }
    void setB(int b) {
        if (b < 0 || b > 255) {
            throw std::invalid_argument("Color value out of range (0-255)");
        }
        b_ = b;
    }
};

TEST_CASE("C++ Class to JS Binding", "[class]") {
    qjs::Engine engine;

    // Register the class and its bindings
    engine.make_class<TestVector>("Vector2")
        .add_constructor<float, float>()
        .add_property("x", &TestVector::x)
        .add_property("y", &TestVector::y)
        .add_method("length", &TestVector::length)
        .add_method("scale", &TestVector::scale);

    engine.make_class<JSColor>("Color")
        .add_constructor<int, int, int>()
        .add_property("r", &JSColor::getR, &JSColor::setR)
        .add_property("g", &JSColor::getG, &JSColor::setG)
        .add_property("b", &JSColor::getB, &JSColor::setB);

    SECTION("Can instantiate the class from JS") {
        auto result = engine.eval(R"(
            let v = new Vector2(3.0, 4.0);
            v !== undefined && v !== null;
        )");
        REQUIRE(result.has_value());
        CHECK(result.value() == "true");
    }

    SECTION("Can read properties bound to native members") {
        std::ignore = engine.eval("let v = new Vector2(5.5, 10.0);");

        auto res_x = engine.eval("v.x");
        REQUIRE(res_x.has_value());
        CHECK(res_x.value() == "5.5");

        auto res_y = engine.eval("v.y");
        REQUIRE(res_y.has_value());
        CHECK(res_y.value() == "10");
    }

    SECTION("Can write properties and mutate the native struct") {
        std::ignore = engine.eval(R"(
            let v = new Vector2(0.0, 0.0);
            v.x = 42.5;
            v.y = 100.0;
        )");

        auto res_x = engine.eval("v.x");
        CHECK(res_x.value() == "42.5");

        auto res_y = engine.eval("v.y");
        CHECK(res_y.value() == "100");
    }

    SECTION("Can invoke bound native methods") {
        std::ignore = engine.eval("let v = new Vector2(3.0, 4.0);");

        // 3^2 + 4^2 = 25 -> sqrt = 5
        auto len_res = engine.eval("v.length()");
        REQUIRE(len_res.has_value());
        CHECK(len_res.value() == "5");
    }

    SECTION("Methods can modify internal state") {
        std::ignore = engine.eval(R"(
            let v = new Vector2(2.0, 3.0);
            v.scale(10.0);
        )");

        auto res_x = engine.eval("v.x");
        CHECK(res_x.value() == "20");

        auto res_y = engine.eval("v.y");
        CHECK(res_y.value() == "30");
    }

    SECTION("Instantiating without 'new' throws a TypeError") {
        // According to our trampoline, omitting 'new' should throw
        auto result = engine.eval("let v = Vector2(1.0, 1.0);");
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().find("TypeError") != std::string::npos);
    }

    SECTION("Can read and write properties via explicit getters and setters") {
        std::ignore = engine.eval("let c = new Color(255, 128, 64);");

        // Test reading properties through the getter methods
        auto res_r = engine.eval("c.r");
        REQUIRE(res_r.has_value());
        CHECK(res_r.value() == "255");

        auto res_g = engine.eval("c.g");
        REQUIRE(res_g.has_value());
        CHECK(res_g.value() == "128");

        // Test writing properties through the setter methods
        std::ignore = engine.eval(R"(
            c.r = 10;
            c.g = 20;
        )");

        auto updated_r = engine.eval("c.r");
        CHECK(updated_r.value() == "10");

        auto updated_g = engine.eval("c.g");
        CHECK(updated_g.value() == "20");
    }

}