#include "../lib/qjswrapper.hpp"
#include <iostream>
#include "raylib.h"

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

void add_console_utilities(qjs::Engine& engine) {
    auto global = engine.get_global_object();
    auto console = engine.create_object("console");

    console.register_function("log", [](std::string msg) {
        std::cout << "[JS] " << msg << std::endl;
    });
}

// this tests running a file with structs and functions
int main() {

    qjs::Engine engine;     

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