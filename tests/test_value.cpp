#include <catch2/catch_all.hpp>
#include "../src/engine.hpp"

TEST_CASE("Value Type Conversions", "[value]") {
    qjs::Engine engine;

    SECTION("Integer conversion") {
        qjs::Value v = engine.make_value(123);
        CHECK(v.is_number());
        CHECK(v.as<int>() == 123);
    }

    SECTION("String conversion") {
        qjs::Value v = engine.make_value("QuickJS");
        CHECK(v.is_string());
        CHECK(v.as<std::string>() == "QuickJS");
    }

    SECTION("Boolean conversion") {
        qjs::Value v = engine.make_value(true);
        CHECK(v.is_bool());
        CHECK(v.as<bool>() == true);
    }
}

TEST_CASE("Value Property Management", "[value]") {
    qjs::Engine engine;
    auto obj = engine.make_object();

    SECTION("Setting and getting variables") {
        obj.set_variable("score", 100);
        auto prop = obj.get_property("score");
        CHECK(prop.is_number());
        CHECK(prop.as<int>() == 100);
    }

    SECTION("Defining constants") {
        obj.set_constant("PI", 3.14);
        CHECK(obj.has_property("PI"));
        CHECK(obj.get_property("PI").as<double>() == Catch::Approx(3.14));
    }

    SECTION("Array elements") {
        auto arr = engine.make_array();
        arr.set_element(0, "first");
        arr.set_element(1, 2);

        CHECK(arr.is_array());
        CHECK(arr.get_element(0).as<std::string>() == "first");
        CHECK(arr.get_element(1).as<int>() == 2);
    }
}

TEST_CASE("Value Resource Management (Rule of Five)", "[value]") {
    qjs::Engine engine;

    SECTION("Move semantics") {
        qjs::Value v1 = engine.make_value("move_me");
        qjs::Value v2 = std::move(v1);

        CHECK(v1.is_undefined());
        CHECK(v2.as<std::string>() == "move_me");
    }

    SECTION("Copy semantics (JS_DupValue)") {
        qjs::Value v1 = engine.make_value("copy_me");
        qjs::Value v2 = v1; // Copy constructor

        CHECK(v1.as<std::string>() == "copy_me");
        CHECK(v2.as<std::string>() == "copy_me");
    }
}

TEST_CASE("Value Type Checkers (is_*)", "[value]") {
    qjs::Engine engine;

    SECTION("is_number") {
        CHECK(engine.make_value(42).is_number());
        CHECK(engine.make_value(3.14).is_number());
        CHECK_FALSE(engine.make_value("hello").is_number());
    }

    SECTION("is_string") {
        CHECK(engine.make_value("hello").is_string());
        CHECK_FALSE(engine.make_value(42).is_string());
    }

    SECTION("is_bool") {
        CHECK(engine.make_value(true).is_bool());
        CHECK(engine.make_value(false).is_bool());
        CHECK_FALSE(engine.make_value(1).is_bool());
    }

    SECTION("is_object") {
        CHECK(engine.make_object().is_object());
        CHECK(engine.make_array().is_object()); // In JS, arrays are objects!
        CHECK_FALSE(engine.make_value(42).is_object());
    }

    SECTION("is_array") {
        CHECK(engine.make_array().is_array());
        CHECK_FALSE(engine.make_object().is_array());
    }

    SECTION("is_undefined") {
        qjs::Value v = engine.make_value(42);
        qjs::Value moved = std::move(v);

        CHECK(v.is_undefined());
        CHECK_FALSE(moved.is_undefined());
    }

    SECTION("Lambda with CHECK macro") {
        auto test_func = [](const int32_t a, const double b) {
            CHECK(a == 42);
            CHECK(b == 3.14);
            return a + b;
        };

        auto global = engine.global();
        global.set_function("testFunc", test_func);

        auto result = engine.eval("testFunc(42, 3.14);");

        CHECK(result.has_value());
        CHECK(result.value() == "45.14");
    }

}