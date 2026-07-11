#include "../include/qjswrapper.hpp"

struct Vector2 {
    float x;
    float y;

    Vector2() : x(0), y(0) {}
    Vector2(float x, float y) : x(x), y(y) {}

    float length() {
        return std::sqrt(x * x + y * y);
    }
};

int main() {
    qjs::Engine engine;

    // The fluent API in action:
    engine.make_class<Vector2>("Vector2")
        .add_constructor<float, float>()
        .add_property("x", &Vector2::x)
        .add_property("y", &Vector2::y)
        .add_method("length", &Vector2::length);

    std::ignore = engine.eval(R"(
        let v = new Vector2(3.0, 4.0);
        console.log("X: " + v.x + ", Y: " + v.y);
        console.log("Length: " + v.length());

        v.x = 10.0; // Automatically triggers the setter
    )");

    return 0;
}