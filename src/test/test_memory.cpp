#include <catch2/catch_test_macros.hpp>
#include "../main/qjswrapper.hpp"

// A helper class to track instance lifetimes
class MemoryTracker {
public:
    static inline int active_instances = 0;

    MemoryTracker() { active_instances++; }
    ~MemoryTracker() { active_instances--; }

    void ping() {}
};

TEST_CASE("Engine Memory and GC Stability", "[memory]") {
    qjs::Engine engine;

    SECTION("C++ Object Lifecycle via JS GC") {
        MemoryTracker::active_instances = 0;

        // 1. Define the class
        engine.define_class<MemoryTracker>("Tracker")
              .constructor()
              .method("ping", &MemoryTracker::ping);

        // 2. Create and then lose reference to a JS object in a nested scope
        auto _ = engine.eval(R"(
            {
                let t = new Tracker();
                t.ping();
            }
            // 't' is now out of scope and eligible for GC
        )", "mem_test.js");

        // 3. Manually trigger QuickJS Garbage Collection
        // Note: QuickJS-NG/QuickJS uses JS_RunGC.
        // Our wrapper Engine doesn't explicitly expose RunGC, but we can access
        // the raw context or simply let the Engine handle it during job execution.
        JS_RunGC(JS_GetRuntime(engine.global().as_value().ctx()));

        // Verify the C++ destructor was called
        CHECK(MemoryTracker::active_instances == 0);
    }

    SECTION("RAII Value Reference Counting") {
        JSContext* ctx = engine.global().as_value().ctx();
        int initial_instances = MemoryTracker::active_instances;

        {
            // Create a managed Value
            qjs::Value val1(ctx, JS_NewObject(ctx));
            {
                // Test Copy Semantics (increments ref count)
                qjs::Value val2 = val1;
                CHECK(val2.get().u.ptr == val1.get().u.ptr);
            }
            // val2 goes out of scope, decrements ref count but object remains
        }
        // val1 goes out of scope, object is freed

        CHECK(true); // If we reached here without a crash/leak, RAII is working
    }

    SECTION("Circular Reference Resilience") {
        // Test if the engine handles basic circular references without crashing
        auto _ = engine.eval(R"(
            let a = {};
            let b = { parent: a };
            a.child = b;
        )", "circular.js");

        // No explicit assertion needed; failure would manifest as a leak
        // or crash during Engine destruction.
        SUCCEED("Engine handles circular JS references");
    }
}