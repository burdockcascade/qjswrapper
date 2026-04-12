#include <catch2/catch_test_macros.hpp>
#include <string>
#include "../src/qjswrapper.hpp"

// A simple stateful class to test class exports
class Counter {
public:
    Counter(int start) : count(start) {}
    void increment() { count++; }
    int get() const { return count; }
private:
    int count;
};

TEST_CASE("Engine Module Support", "[engine][module]") {
    qjs::Engine engine;

    // 1. Define the class in the engine
    auto counterClass = engine.define_class<Counter>("Counter")
        .constructor<int>()
        .method("increment", &Counter::increment)
        .method("get", &Counter::get);

    // 2. Define the module and export our C++ primitives and classes
    auto testModule = engine.define_module("TestUtils");

    testModule.add("Counter", counterClass);
    testModule.add("VERSION", "1.2.3");
    testModule.add("multiply", [](int a, int b) {
        return a * b;
    });

    // 3. Evaluate the ES6 module
    // We attach our test outputs to 'globalThis' so C++ can easily read them afterwards.
    std::string js_code = R"(
        import { Counter, VERSION, multiply } from "TestUtils";

        // Create a global object to store our test results
        globalThis.moduleResults = {
            version: VERSION,
            mathResult: multiply(4, 5),
            counterFinal: 0
        };

        // Test the exported C++ class
        let c = new Counter(10);
        c.increment();
        c.increment();
        globalThis.moduleResults.counterFinal = c.get();
    )";

    // Run the module! (This triggers the pending jobs loop we added earlier)
    auto result = engine.eval(js_code, "test_module.js", qjs::EvalMode::Module);

    // Ensure the script compiled and ran without throwing JS exceptions
    REQUIRE(result.has_value());

    // 4. Extract the results from the global context
    auto resultsObj = engine.global().get<qjs::Object>("moduleResults");

    // 5. Assert that the JavaScript logic correctly utilized the C++ native module
    REQUIRE(resultsObj.get<std::string>("version") == "1.2.3");
    REQUIRE(resultsObj.get<int>("mathResult") == 20);
    REQUIRE(resultsObj.get<int>("counterFinal") == 12);
}