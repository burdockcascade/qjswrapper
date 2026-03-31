#include "raylib.h"
#include "qjswrapper.hpp"

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


int cpp_add(const int a, const int b) {
    return a + b;
}

int test_math() {
    qjs::Engine engine;

    // Bind any C++ lambda
    engine.register_function("add", [](int a, int b) { return a + b; });

    // Evaluate and get result (or error)
    auto res = engine.eval("add(5, 10)");

    if (res) {
        std::cout << "Result: " << *res << "\n";
    } else {
        std::cerr << "JS Error: " << res.error() << "\n";
    }
    return 0;
}

int test_raylib() {
    // 1. Setup Raylib
    InitWindow(800, 450, "QuickJS + Raylib File Loader");
    SetTargetFPS(60);

    qjs::Engine engine;

    // 2. Bind the Raylib functions we need
    engine.register_function("clearBackground", [](Color c) { ClearBackground(c); });
    engine.register_function("drawCircle", [](int x, int y, float r, Color c) { DrawCircle(x, y, r, c); });
    engine.register_function("drawText", [](std::string text, int x, int y, int size, Color c) {
        DrawText(text.c_str(), x, y, size, c);
    });

    // 3. Main Game Loop
    while (!WindowShouldClose()) {
        BeginDrawing();

        // Execute the external script every frame
        // Note: In a real game, you'd likely load this once and call a JS 'update' function
        auto result = engine.run_file(R"(C:\workspace\c\qjswrapper\src\game.js)");

        if (!result) {
            DrawText(result.error().c_str(), 10, 10, 20, RED);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}

// --- Usage ---
int main() {
    test_raylib();
}