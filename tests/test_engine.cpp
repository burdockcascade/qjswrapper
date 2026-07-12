#include <catch2/catch_all.hpp>
#include "../src/qjswrapper.hpp"

TEST_CASE("Engine Lifecycle and Evaluation", "[engine]") {
    qjswrapper::Engine engine;

    SECTION("Basic script evaluation returns stringified result") {
        auto result = engine.eval("1 + 2");
        REQUIRE(result.has_value());
        CHECK(result.value() == "3");
    }

    SECTION("Evaluation handles syntax errors") {
        auto result = engine.eval("invalid core;");
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().find("SyntaxError") != std::string::npos);
    }

    SECTION("Accessing global object") {
        auto global = engine.global();
        CHECK(global.is_object());

        auto _ = engine.eval("var x = 42;");
        auto x = global.get_property("x");
        CHECK(x.as<int>() == 42);
    }
}

TEST_CASE("Engine Value Factories", "[engine]") {
    qjswrapper::Engine engine;

    SECTION("Factory methods create correct types") {
        auto val_int = engine.make_value(10);
        auto val_str = engine.make_value("hello");
        auto val_obj = engine.make_object();
        auto val_arr = engine.make_array();

        CHECK(val_int.is_number());
        CHECK(val_str.is_string());
        CHECK(val_obj.is_object());
        CHECK(val_arr.is_array());
    }
}