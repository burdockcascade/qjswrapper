# QJSWrapper
QJSWrapper is a modern, lightweight C++23 wrapper around QuickJS-NG. It aims to simplify the integration of JavaScript into C++ applications by providing a high-level, type-safe API for managing engines, objects, functions, and classes.

## Features
* Modern C++ API: Utilizes C++23 features like std::expected for robust error handling.
* Automatic Type Conversion: Seamlessly convert between C++ types and JavaScript values.
* Class Binding: Register C++ classes to be used in JavaScript with support for multiple constructors and method binding.
* Module System: Define and export native C++ modules to JavaScript.
* Object Management: Easily manipulate JavaScript objects and properties with a fluent interface.

## Examples
To use QJSWrapper, include the main header in your project:
```c++
#include "qjswrapper.hpp"
```

### Basic Execution and Function Registration
You can register C++ lambdas or functions directly into the global JavaScript scope.

```c++
#include <iostream>
#include "qjswrapper.hpp"

int main() {
    qjs::Engine engine;
    auto global = engine.global();

    // Register C++ functions to JavaScript
    global.set_function("add", [](int a, int b) { return a + b; });

    // Execute JavaScript code
    auto result = engine.eval("add(10, 5)", "main.js");

    if (result) {
        std::cout << "Result: " << *result << std::endl; // Output: 15
    } else {
        std::cerr << "Error: " << result.error() << std::endl;
    }

    return 0;
}
```

### Working with Objects and Properties
QJSWrapper allows you to create objects and set properties with specific attributes, such as ReadOnly.

```c++
#include "qjswrapper.hpp"

int main() {
    qjs::Engine engine;
    auto config = engine.make_object();
    
    // Set properties
    config.set_variable("theme", "dark")
          .set_constant("version", "1.0.4");
    
    engine.global().set("config", config);
    
    // JavaScript cannot overwrite read-only properties in "use strict" mode
    engine.eval(R"(
        "use strict";
        config.theme = "light"; // Works
        config.version = "2.0.0"; // Throws TypeError
    )", "test.js");
    
    return 0;
}
```

### Binding C++ Classes
You can expose C++ classes to JavaScript, including constructors, methods, and constants.

```c++
#include "qjswrapper.hpp"

class Player {
public:
    Player(std::string name, int health) : name_(name), health_(health) {}
    void heal(int amount) { health_ += amount; }
    int getHealth() const { return health_; }

private:
    std::string name_;
    int health_;
};

int main() {
    qjs::Engine engine;
    
    auto player_class = engine.define_class<Player>("Player");
    player_class
        .constructor<std::string, int>() // Default constructor
        .method("heal", &Player::heal)
        .method("getHealth", &Player::getHealth);
    
    // Use in JavaScript
    engine.eval(R"(
        const hero = new Player("Arthur", 80);
        hero.heal(20);
        console.log(hero.getHealth()); // 100
    )", "game.js");
    
    return 0;
}
```

### Native Modules
Define native modules in C++ that can be imported in JavaScript using the import statement.
qjs::Engine engine;

```c++
#include "qjswrapper.hpp"

int main() {
    qjs::Engine engine;
    
    auto my_module = engine.define_module("math_utils");
    my_module.set_constant("PI", 3.14159);
    my_module.set_function("square", [](double x) { return x * x; });
    
    // Use in JavaScript (requires eval_module)
    engine.eval(R"(
    import { PI, square } from "math_utils";
    const area = PI * square(5);
    )", "app.js", qjs::EvalMode::Module);
    
    return 0;
}
```

## Requirements
* Compiler: C++23 compliant compiler (e.g., GCC 13+, Clang 16+, MSVC 19.36+).
* Dependency: QuickJS-NG.

## License
This project is licensed under the terms found in the LICENSE file.