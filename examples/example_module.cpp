#include "../include/qjswrapper.hpp"
#include <iostream>
#include <string>

// 1. A simple C++ class we want to expose
class Rectangle {
public:
    Rectangle(double w, double h) : width(w), height(h) {}

    [[nodiscard]] double getArea() const {
        return width * height;
    }

    void scale(const double factor) {
        width *= factor;
        height *= factor;
    }

private:
    double width;
    double height;
};

int main() {
    qjs::Engine engine;

    // Optional: Bind a simple console.log so we can see the output of our JS!
    engine.global().set("console", engine.make_object()
        .set("log", [](const std::string& msg) {
            std::cout << "> " << msg << std::endl;
        })
    );

    // 2. Define the class and its methods in the wrapper
    auto rectClass = engine.define_class<Rectangle>("Rectangle")
        .constructor<double, double>()
        .method("getArea", &Rectangle::getArea)
        .method("scale", &Rectangle::scale);

    // 3. Define the native module and export our bindings
    auto mathModule = engine.define_module("Geometry");

    // Export the C++ class
    mathModule.add("Rectangle", rectClass);

    // Export a static constant
    mathModule.add("PI", 3.14159265359);

    // Export a standalone C++ lambda function
    mathModule.add("calculateCircleArea", [](double radius) {
        return 3.14159265359 * radius * radius;
    });

    // 4. Test it with ES6 Module syntax in JS
    std::string js_code = R"(
        // Import our native C++ bindings!
        import { Rectangle, PI, calculateCircleArea } from "Geometry";

        console.log("--- Testing Constants & Functions ---");
        console.log("Value of PI is: " + PI);

        let r = 5;
        let circleArea = calculateCircleArea(r);
        console.log("Circle area with radius 5 is: " + circleArea);

        console.log("--- Testing C++ Class ---");
        let myRect = new Rectangle(10, 5);
        console.log("Rectangle initial area: " + myRect.getArea());

        myRect.scale(2); // Scales width and height by 2
        console.log("Rectangle area after 2x scale: " + myRect.getArea());
    )";

    // Run the module using eval_module to support 'import' syntax
    auto result = engine.eval(js_code, "main.js", qjs::EvalMode::Module);

    if (!result) {
        std::cerr << "Script error: " << result.error() << std::endl;
    }

    return 0;
}