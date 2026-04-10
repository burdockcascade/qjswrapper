#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "../src/qjswrapper.hpp"

using Catch::Matchers::ContainsSubstring;

TEST_CASE("Engine Lifecycle and Multi-Runtime Isolation", "[engine]") {
    SECTION("Sequential Engine Creation") {
        for (int i = 0; i < 3; ++i) {
            qjs::Engine local_engine;
            local_engine.global().set("func", []() { return 42; });
            auto res = local_engine.eval("func()", "test.js");
            REQUIRE(res.has_value());
            CHECK(res.value() == "42");
        } // local_engine goes out of scope here, cleaning up Runtime and Context
    }
}