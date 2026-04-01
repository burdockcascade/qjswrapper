#include <utility>

#include "raylib.h"
#include "lib/qjswrapper.hpp"
#include "js/ship.h"

namespace qjs {

    template<>
    struct converter<Color> {
        static Color get(JSContext* ctx, JSValueConst v) {
            uint32_t r, g, b, a;
            const JSValue r_val = JS_GetPropertyStr(ctx, v, "r");
            const JSValue g_val = JS_GetPropertyStr(ctx, v, "g");
            const JSValue b_val = JS_GetPropertyStr(ctx, v, "b");
            const JSValue a_val = JS_GetPropertyStr(ctx, v, "a");

            JS_ToUint32(ctx, &r, r_val);
            JS_ToUint32(ctx, &g, g_val);
            JS_ToUint32(ctx, &b, b_val);
            JS_ToUint32(ctx, &a, a_val);

            JS_FreeValue(ctx, r_val); JS_FreeValue(ctx, g_val);
            JS_FreeValue(ctx, b_val); JS_FreeValue(ctx, a_val);

            return Color{static_cast<unsigned char>(r), static_cast<unsigned char>(g), static_cast<unsigned char>(b), static_cast<unsigned char>(a)};
        }
        static JSValue put(JSContext* ctx, Color val) {
            const JSValue obj = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, obj, "r", JS_NewInt32(ctx, val.r));
            JS_SetPropertyStr(ctx, obj, "g", JS_NewInt32(ctx, val.g));
            JS_SetPropertyStr(ctx, obj, "b", JS_NewInt32(ctx, val.b));
            JS_SetPropertyStr(ctx, obj, "a", JS_NewInt32(ctx, val.a));
            return obj;
        }
    };

    template<>
    struct converter<Vector2> {
        static Vector2 get(JSContext* ctx, JSValueConst v) {
            double x, y;
            const JSValue x_val = JS_GetPropertyStr(ctx, v, "x");
            const JSValue y_val = JS_GetPropertyStr(ctx, v, "y");
            JS_ToFloat64(ctx, &x, x_val);
            JS_ToFloat64(ctx, &y, y_val);
            JS_FreeValue(ctx, x_val); JS_FreeValue(ctx, y_val);
            return Vector2{static_cast<float>(x), static_cast<float>(y)};
        }
        static JSValue put(JSContext* ctx, Vector2 val) {
            const JSValue obj = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, val.x));
            JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, val.y));
            return obj;
        }
    };

}


int cpp_sub(const int a, const int b) {
    return a - b;
}

int test_math(qjs::Engine &engine) {

    auto global = engine.get_global_object();

    // Bind any C++ lambda
    global.register_function("add", [](const int a, const int b) { return a + b; });
    global.register_function("sub", cpp_sub);

    // Evaluate and get result (or error)
    auto res = engine.eval("add(5, sub(20 - 10))");

    if (res) {
        std::cout << "Result: " << *res << "\n";
    } else {
        std::cerr << "JS Error: " << res.error() << "\n";
    }
    return 0;
}

class Ship {
public:
    Ship(std::string name, int fuel) : name(name), fuel(fuel) {
        std::cout << "[C++] Ship '" << name << "' constructed with " << fuel << " fuel.\n";
    }

    void fly(int distance) {
        if (fuel >= distance) {
            fuel -= distance;
            std::cout << "[C++] " << name << " flew " << distance << " units. Fuel left: " << fuel << "\n";
        } else {
            std::cout << "[C++] " << name << " out of fuel!\n";
        }
    }

    int get_fuel() const { return fuel; }
    std::string get_name() const { return name; }

    std::string name;
    int fuel;
};

int test_ship(qjs::Engine &engine) {

    auto global = engine.get_global_object();

    global.register_class<Ship>("Ship")
        .constructor([](const std::string& name, int fuel) {
            auto s = new Ship(name, fuel);
            return s;
        })
        .method("fly", &Ship::fly)
        .method("refuel", [](Ship* s, int amount) {
            s->fuel += amount;
            std::cout << s->name << " refueled by " << amount << std::endl;
        })
        .method("getFuel", &Ship::get_fuel)
        .method("getName", &Ship::get_name)
        .field("fuel", &Ship::fuel)
        .field("name", &Ship::name);

    // Fix warning C4834 by handling the expected result
    if (auto result = engine.run_bytecode(qjsc_ship, qjsc_ship_size); !result) {
        std::cerr << "Script Error: " << result.error() << "\n";
    }

    return 0;
}

// this tests running a file with structs and functions
int test_raylib(qjs::Engine &engine) {
    // 1. Setup Raylib
    InitWindow(800, 450, "QuickJS + Raylib File Loader");
    SetTargetFPS(60);

    auto global = engine.get_global_object();
    auto rl = global.create_object("rl");

    // 2. Bind the Raylib functions we need
    rl.register_function("clearBackground", ::ClearBackground);
    rl.register_function("drawCircleV", [](const Vector2 v2, float r, Color c) { DrawCircleV(v2, r, c); });
    rl.register_function("drawText", [](const std::string& text, int x, int y, int size, Color c) {
        DrawText(text.c_str(), x, y, size, c);
    });

    global.register_constant("LOG_ALL", 0);

    global.register_class<Color>("Color")
        .constructor<unsigned char, unsigned char, unsigned char, unsigned char>()
        .field("r", &Color::r)
        .field("g", &Color::g)
        .field("b", &Color::b)
        .field("a", &Color::a)
        .static_constant("BLUE", BLUE)
        .static_constant("RED", RED)
        .static_constant("GOLD", GOLD)
        .static_method("randomColor", [] {
            return Color{
                static_cast<unsigned char>(GetRandomValue(0, 255)),
                static_cast<unsigned char>(GetRandomValue(0, 255)),
                static_cast<unsigned char>(GetRandomValue(0, 255)),
                255
            };
        });

    global.register_class<Vector2>("Vector2")
        .constructor<float, float>()
        .field("x", &Vector2::x)
        .field("y", &Vector2::y)
        .method("add", [](Vector2 *v1, Vector2 v2) {
            v1->x += v2.x;
            v1->y += v2.y;
            return v1;
        });

    // 3. Main Game Loop
    while (!WindowShouldClose()) {
        BeginDrawing();

        // Execute the external script every frame
        // Note: In a real game, you'd likely load this once and call a JS 'update' function
        auto result = engine.run_file(R"(C:\workspace\c\qjswrapper\src\js\game.js)");

        if (!result) {
            DrawText(result.error().c_str(), 10, 10, 20, RED);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}

void add_console_utilities(qjs::Engine& engine) {
    auto global = engine.get_global_object();
    auto console = engine.create_object("console");

    console.register_function("log", [](std::string msg) {
        std::cout << "[JS] " << msg << std::endl;
    });
}

// --- Usage ---
int main(int argc, char* argv[]) {
    // Check if an argument was provided
    if (argc < 2) {
        std::cout << "Usage: qjs_test [test_name]\n";
        std::cout << "Available tests: math, ship, raylib\n";
        return 1;
    }

    // Convert the argument to a std::string for easy comparison
    std::string test_to_run = argv[1];

    qjs::Engine engine;

    add_console_utilities(engine);

    // Dispatch to the correct test function
    if (test_to_run == "math") {
        std::cout << "--- Running Math Test ---\n";
        return test_math(engine);
    }
    else if (test_to_run == "ship") {
        std::cout << "--- Running Ship Test ---\n";
        return test_ship(engine);
    }
    else if (test_to_run == "raylib") {
        std::cout << "--- Running Raylib Test ---\n";
        return test_raylib(engine);
    }
    else {
        std::cerr << "Unknown test: " << test_to_run << "\n";
        std::cerr << "Try: math, ship, or raylib\n";
        return 1;
    }
}