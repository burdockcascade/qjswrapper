#include <iostream>
#include <fstream>
#include <vector>
#include "../include/qjswrapper.hpp"

// A quick helper function to create a dummy JS file for our compiler to read
void create_temp_file(const std::string& filename, const std::string& content) {
    std::ofstream f(filename);
    f << content;
}

int main() {
    std::cout << "--- QuickJS Bytecode Example ---\n\n";

    // 1. Initialize our wrapper engine
    qjs::Engine engine;

    // 2. Create a standard JavaScript module file on disk
    std::string module_filename = "math_module.js";
    create_temp_file(module_filename,
        "export function add(a, b) { return a + b; }\n"
        "export function multiply(a, b) { return a * b; }\n"
    );
    std::cout << "[Step 1] Created source file: " << module_filename << "\n";

    // 3. Compile the module into bytecode
    // We pass 'true' because this is an ES6 module
    auto compiled_result = engine.compile_file_to_bytecode(module_filename, qjs::EvalMode::Module);

    if (!compiled_result) {
        std::cerr << "Compilation failed: " << compiled_result.error() << "\n";
        return 1;
    }

    const std::vector<uint8_t>& bytecode = compiled_result.value();
    std::cout << "[Step 2] Successfully compiled! Bytecode size: " << bytecode.size() << " bytes.\n";

    // 4. Register the bytecode as an embedded module
    // This simulates having the bytecode embedded directly in your C++ executable (e.g., via xxd or qjsc)
    engine.register_module_bytecode("MathLib", bytecode.data(), bytecode.size());
    std::cout << "[Step 3] Registered bytecode into memory under the name 'MathLib'.\n";

    // 5. Run a script that imports and uses our embedded bytecode module
    std::cout << "[Step 4] Executing a JS script that imports 'MathLib'...\n";
    std::string main_script =
        "import { add, multiply } from 'MathLib';\n"
        "globalThis.resultAdd = add(10, 5);\n"
        "globalThis.resultMult = multiply(10, 5);\n";

    auto eval_result = engine.eval(main_script, "main.js", qjs::EvalMode::Module);

    if (!eval_result) {
        std::cerr << "Execution failed: " << eval_result.error() << "\n";
        return 1;
    }

    // 6. Verify the results from C++
    int add_val = engine.global().get<int>("resultAdd");
    int mult_val = engine.global().get<int>("resultMult");

    std::cout << "\n--- Results calculated in JS ---\n";
    std::cout << "10 + 5 = " << add_val << "\n";
    std::cout << "10 * 5 = " << mult_val << "\n";

    // Clean up the temp file so we don't leave a mess
    std::remove(module_filename.c_str());

    return 0;
}