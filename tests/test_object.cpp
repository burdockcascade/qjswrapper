#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <algorithm>
#include "../src/qjswrapper.hpp"

using Catch::Matchers::ContainsSubstring;

// --- 1. CORE DATA HANDLING ---
TEST_CASE("Object Data Management", "[object][data]") {
    qjs::Engine engine;
    auto& global = engine.global();

    SECTION("Primitive Type Round-tripping") {
        global.set("a_bool", true)
              .set("a_int", 123)
              .set("a_double", 45.67)
              .set("a_string", "Hello QuickJS");

        // Verify native extraction
        CHECK(global.get<bool>("a_bool") == true);
        CHECK(global.get<int>("a_int") == 123);
        CHECK(global.get<double>("a_double") == 45.67);
        CHECK(global.get<std::string>("a_string") == "Hello QuickJS");

        // Verify JS eval extraction
        CHECK(engine.eval_global("a_bool", "test.js").value() == "true");
        CHECK(engine.eval_global("a_int", "test.js").value() == "123");
    }

    SECTION("Type Coercion and Converters") {
        auto _ = engine.eval_global("var test_float = 99.9;", "test.js");
        // Verify converter handles float-to-int truncation
        CHECK(global.get<int>("test_float") == 99);
    }

    SECTION("Deep Object Nesting") {
        auto inner = engine.make_object().set("secret", 777);
        auto outer = engine.make_object().set("inner", inner);
        global.set("outer", outer);

        auto result = engine.eval_global("outer.inner.secret", "test.js");
        REQUIRE(result.has_value());
        CHECK(result.value() == "777");
    }
}

// --- 2. LAMBDA & FUNCTION BINDING ---
TEST_CASE("Function and Lambda Binding", "[object][function]") {
    qjs::Engine engine;

    SECTION("Basic Lambda Handling") {
        auto math = engine.make_object();
        math.set("multiply", [](double a, double b) { return a * b; });
        math.set("greet", []() { return "Hello"; });

        engine.global().set("math", math);

        CHECK(engine.eval_global("math.multiply(2.5, 4)", "test.js").value() == "10");
        CHECK(engine.eval_global("math.greet()", "test.js").value() == "Hello");
    }

    SECTION("Capture Semantics and Side Effects") {
        int call_count = 0;
        engine.global().set("tick", [&]() { call_count++; });

        auto _ = engine.eval_global("tick(); tick();", "test.js");
        CHECK(call_count == 2);
    }

    SECTION("std::function Traits Deduction") {
        std::string prefix = "Log: ";
        std::function<std::string(std::string)> logger = [&](std::string m) { return prefix + m; };

        engine.global().set("log", logger);
        prefix = "Trace: "; // Verify capture by reference works
        CHECK(engine.eval_global("log('Msg')", "test.js").value() == "Trace: Msg");
    }
}

// --- 3. NATIVE OBJECT METHODS (REFLECTION) ---
TEST_CASE("Native Object Operations", "[object][methods]") {
    qjs::Engine engine;

    SECTION("Key Enumeration (keys)") {
        auto obj = engine.make_object().set("a", 1).set("b", 2);
        auto keys = obj.keys();

        REQUIRE(keys.size() == 2);
        CHECK(std::find(keys.begin(), keys.end(), "a") != keys.end());
        CHECK(std::find(keys.begin(), keys.end(), "b") != keys.end());
    }

    SECTION("Property Removal and Flags") {
        auto obj = engine.make_object();
        obj.set("normal", 1, qjs::Prop::Normal);
        obj.set("locked", 2, qjs::Prop::Locked);

        CHECK(obj.remove("normal") == true);
        CHECK(obj.remove("locked") == false); // Cannot delete Locked properties
    }

    SECTION("Native Invocation (invoke)") {
        auto obj = engine.make_object();
        obj.set("add", [](int a, int b) { return a + b; });

        auto res = obj.invoke("add", 10, 5);
        CHECK(qjs::converter<int>::get(res.ctx(), res.get()) == 15);
    }
}

// --- 4. ERROR HANDLING & SAFETY ---
TEST_CASE("Error Handling and Stability", "[object][safety]") {
    qjs::Engine engine;

    SECTION("JavaScript Syntax and Runtime Errors") {
        auto result = engine.eval_global("nonExistent()", "test.js");
        REQUIRE_FALSE(result.has_value());
        CHECK_THAT(result.error(), ContainsSubstring("is not defined"));
    }

    SECTION("C++ Exception Propagation") {
        engine.global().set("thrower", []() {
            throw std::runtime_error("C++ error!");
        });

        auto result = engine.eval_global("thrower()", "test.js");
        REQUIRE_FALSE(result.has_value()); // Bridge should convert to JS exception
        CHECK_THAT(result.error(), ContainsSubstring("C++ error!"));
    }
}

TEST_CASE("Object Advanced Coverage", "[object][extra]") {
    qjs::Engine engine;

    SECTION("Object-to-Object conversion") {
        auto parent = engine.make_object();
        auto child = engine.make_object();
        child.set("id", 101);

        // Tests the converter<Object> specialization
        parent.set("child_node", child);

        auto extracted = parent.get<qjs::Object>("child_node");
        CHECK(extracted.get<int>("id") == 101);
    }

    SECTION("Hidden Property Visibility") {
        auto obj = engine.make_object();
        obj.set("visible", 1, qjs::Prop::Normal);
        obj.set("secret", 2, qjs::Prop::Hidden); //

        auto keys = obj.keys();
        CHECK(std::find(keys.begin(), keys.end(), "visible") != keys.end());
        // 'secret' should not be in the keys() vector
        CHECK(std::find(keys.begin(), keys.end(), "secret") == keys.end());
        CHECK(obj.get<int>("secret") == 2); // But it should still be accessible
    }

    SECTION("as_value() consistency") {
        auto obj = engine.make_object();
        obj.set("id", 123);

        qjs::Value managed_val = obj.as_value();

        // Extract raw pointers to compare them as simple types
        auto raw1 = managed_val.get().u.ptr;
        auto raw2 = obj.as_value().get().u.ptr;

        CHECK(raw1 == raw2);
    }
}