#include <string>
#include <raylib.h>
#include <print>
#include "../include/qjswrapper.hpp"

void make_raylib_module(qjs::Engine &engine) {

    // Define the Raylib module
    auto raylib_mod = engine.make_module("Raylib");

    // Text
    auto text_obj = engine.make_object()
        .set_function("DrawText", [](const std::string& text, int x, int y, int fontSize, const Color color) {
            ::DrawText(text.data(), x, y, fontSize, color);
        });
    raylib_mod.add_value("Text", text_obj);

    // Core
    auto core_obj = engine.make_object()
        .set_function("InitWindow", [](const int width, const int height, const std::string &title) {
            ::InitWindow(width, height, title.data());
        })
        .set_function("CloseWindow", ::CloseWindow)
        .set_function("WindowShouldClose", ::WindowShouldClose)
        .set_function("BeginDrawing", ::BeginDrawing)
        .set_function("EndDrawing", ::EndDrawing)
        .set_function("ClearBackground", ::ClearBackground);
    raylib_mod.add_value("Core", core_obj);

    auto color_class = engine.make_class<Color>("Color")
        .add_constructor()
        .add_constructor<unsigned char, unsigned char, unsigned char, unsigned char>()
        .add_property("r", &Color::r)
        .add_property("g", &Color::g)
        .add_property("b", &Color::b)
        .add_property("a", &Color::a)
        .build();
    raylib_mod.add_value("Color", color_class);

    // Palette
    auto palette_obj = engine.make_object()
        .set_constant("BLUE", ::BLUE);
    raylib_mod.add_value("Palette", palette_obj);
}

int main() {

    // Initialize the engine
    qjs::Engine engine;

    // Define the script
    const std::string script = R"(
        import { Core, Text, Color, Palette } from "Raylib";

        Core.InitWindow(800, 600, "Hello Raylib Module!");

        // Instantiate C++ struct objects natively
        const bgColor = new Color(255, 255, 255, 255);
        const textColor = Palette.BLUE;

        while (!Core.WindowShouldClose()) {
            Core.BeginDrawing();
            Core.ClearBackground(Color.WHITE);
            Text.DrawText("Hello Raylib Module!", 190, 200, 20, textColor);
            Core.EndDrawing();
        }
        Core.CloseWindow();
    )";

    // Create the Raylib module
    make_raylib_module(engine);

    // Execute the script
    if (auto result = engine.eval(script, "raylib.js", qjs::EvalType::Module)) {
        std::println("Script executed successfully.");
    } else {
        const auto& err = result.error();
        std::println(stderr, "{}", err);
    }

    return 0;
}