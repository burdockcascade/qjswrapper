#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include "../src/qjswrapper.hpp"

namespace fs = std::filesystem;

TEST_CASE("Engine Bytecode Features", "[engine][bytecode]") {
    qjs::Engine engine;
    constexpr std::string test_file = "temp_test.js";

    // Helper to create a temporary JS file
    auto create_file = [&](const std::string& path, const std::string& content) {
        std::ofstream f(path);
        f << content;
    };

    SECTION("Compile and Run Bytecode (Global)") {
        create_file(test_file, "1 + 2");

        // 1. Compile file to bytecode
        auto compile_res = engine.compile_file_to_bytecode(test_file, qjs::EvalMode::Script);
        REQUIRE(compile_res.has_value());
        const std::vector<uint8_t>& bytecode = compile_res.value();
        REQUIRE(!bytecode.empty());

        // 2. Execute the compiled bytecode
        auto run_res = engine.eval_bytecode(bytecode.data(), bytecode.size());
        REQUIRE(run_res.has_value());
        CHECK(run_res.value() == "3");

        fs::remove(test_file);
    }

    SECTION("Bytecode Module Registration and Importing") {
        constexpr std::string mod_file = "my_module.js";
        create_file(mod_file, "export const data = 'bytecode_secret';");

        // 1. Compile the file as an ES6 module
        auto compile_res = engine.compile_file_to_bytecode(mod_file, qjs::EvalMode::Module);
        REQUIRE(compile_res.has_value());
        const auto& bytecode = compile_res.value();

        // 2. Register this bytecode as a named module
        engine.register_module_bytecode("SecretModule", bytecode.data(), bytecode.size());

        // 3. Import from the bytecode module and assign to a global variable
        std::string importer_js = R"(
            import { data } from "SecretModule";
            globalThis.testResult = data;
        )";

        // eval_module spins the event loop until jobs (like module resolution) are done
        auto result = engine.eval(importer_js, "importer.js", qjs::EvalMode::Module);
        REQUIRE(result.has_value());

        if (!result.has_value()) {
            UNSCOPED_CAPTURE(result);
        }

        // 4. Retrieve the actual string from the global object instead of the Promise return value
        auto final_value = engine.global().get<std::string>("testResult");
        CHECK(final_value == "bytecode_secret");

        fs::remove(mod_file);
    }

    SECTION("Error Handling: Compilation Failure") {
        create_file(test_file, "const a = ;"); // Syntax Error

        auto compile_res = engine.compile_file_to_bytecode(test_file);
        REQUIRE_FALSE(compile_res.has_value());

        // Extract error for more flexible checking
        const std::string& err_msg = compile_res.error();

        // Check for "unexpected token" instead of "expecting expression"
        bool is_syntax_error = err_msg.find("unexpected token") != std::string::npos ||
                               err_msg.find("SyntaxError") != std::string::npos;

        CHECK(is_syntax_error);

        fs::remove(test_file);
    }

    SECTION("Error Handling: Missing File") {
        auto compile_res = engine.compile_file_to_bytecode("non_existent.js");
        REQUIRE_FALSE(compile_res.has_value());
        CHECK(compile_res.error() == "File not found: non_existent.js");
    }
}