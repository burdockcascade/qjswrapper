#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>
#include <string>
#include <atomic>
#include "../src/qjswrapper.hpp"

TEST_CASE("Engine Thread Isolation and Concurrency", "[concurrency]") {
    const int num_threads = 8;
    const int iterations_per_thread = 50;
    std::atomic<int> total_success_count{0};

    auto thread_func = [&](int thread_id) {
        try {
            // Each thread must have its own independent Engine
            qjs::Engine engine;

            // Register a unique variable for this thread
            std::string var_name = "thread_val_" + std::to_string(thread_id);
            engine.global().set(var_name, thread_id);

            for (int i = 0; i < iterations_per_thread; ++i) {
                // Perform a simple calculation
                auto script = var_name + " + 1";
                auto result = engine.eval(script, "test.js");

                if (result.has_value() && result.value() == std::to_string(thread_id + 1)) {
                    ++total_success_count;
                }

                // Stress the class registration system concurrently
                struct ConcurrentData { int val; };
                auto cls = engine.define_class<ConcurrentData>("Data_" + std::to_string(i))
                                .constructor<int>();
            }
        } catch (...) {
            // If any thread crashes due to race conditions, this will fail the test
        }
    };

    SECTION("Parallel Engine Execution") {
        std::vector<std::jthread> workers;
        for (int i = 0; i < num_threads; ++i) {
            workers.emplace_back(thread_func, i);
        }

        // jthreads automatically join on destruction
        workers.clear();

        CHECK(total_success_count == (num_threads * iterations_per_thread));
    }
}