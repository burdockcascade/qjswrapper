#include <iostream>
#include <string>
#include <raylib.h>
#include <print>
#include "../include/qjswrapper.hpp"

/**
 * We need to convert between JSON and a Raylib Color Struct
 */
template<>
struct qjs::converter<Color> {
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
        return qjs::Value(ctx, obj);
    }
};

void make_raylib_module(qjs::Engine &engine) {

    // Define the Raylib module
    auto raylib_mod = engine.define_module("Raylib");

    // Text
    auto text_obj = engine.make_object();
    text_obj.set_function("DrawText", ::DrawText);
    raylib_mod.add("Text", text_obj);

    // Core
    auto core_obj = engine.make_object();
    core_obj.set_function("InitWindow", ::InitWindow);
    core_obj.set_function("CloseWindow", ::CloseWindow);
    core_obj.set_function("WindowShouldClose", ::WindowShouldClose);
    core_obj.set_function("BeginDrawing", ::BeginDrawing);
    core_obj.set_function("EndDrawing", ::EndDrawing);
    core_obj.set_function("ClearBackground", ::ClearBackground);
    raylib_mod.add("Core", core_obj);

}

int main() {

    // Initialize the engine
    qjs::Engine engine;

    // Define the script
    const std::string script = R"(
        import { Core, Text } from "Raylib";

        Core.InitWindow(800, 600, "Hello Raylib Module!");

        while (!Core.WindowShouldClose()) {
            Core.BeginDrawing();
            Core.ClearBackground({ r: 255, g: 255, b: 255, a: 255 });
            Text.DrawText("Hello Raylib Module!", 190, 200, 20, { r: 255, g: 0, b: 0, a: 255 });
            Core.EndDrawing();
        }
        Core.CloseWindow();
    )";

    // Create the Raylib module
    make_raylib_module(engine);

    // Execute the script
    if (auto result = engine.eval(script, "raylib.js", qjs::EvalMode::Module)) {
        std::println("Script executed successfully.");
    } else {
        const auto& err = result.error();
        std::println(stderr, "{}", err.to_string());
    }

    return 0;
}