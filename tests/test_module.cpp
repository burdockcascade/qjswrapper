#include <catch2/catch_all.hpp>
#include "../src/qjswrapper.hpp"

TEST_CASE("Native ECMAScript Modules", "[module]") {
    qjswrapper::Engine engine;

    // 1. Build a native module fluently
    engine.make_module("math")
        .add_value("PI", 3.14159)
        .add_function("multiply", [](double a, double b) { return a * b; });

    SECTION("Can import native module variables and functions") {
        // 2. Evaluate script as a MODULE
        auto result = engine.eval(R"(
            import { PI, multiply } from 'math';

            // Note: Modules don't return the last expression directly to the C++ eval call.
            // We set it to a global so we can check it in the test.
            globalThis.test_result = multiply(PI, 2.0);
        )", "<eval>", qjswrapper::EvalType::Module);

        REQUIRE(result.has_value());

        auto global = engine.global();
        auto val = global.get_property("test_result");

        REQUIRE(val.as<double>().has_value());
        CHECK(val.as<double>().value() == 6.28318);
    }
}