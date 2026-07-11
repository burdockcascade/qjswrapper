#pragma once

#include <quickjs.h>
#include <expected>
#include <fstream>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <stdexcept>
#include <unordered_set>
#include <cstring>
#include <shared_mutex>
#include <mutex>

#include "value.hpp"
#include "class.hpp"
#include "module.hpp"

namespace qjs {

    enum class EvalType {
        Global,
        Module
    };

    class Engine {
    public:

        Engine() {
            rt = JS_NewRuntime();
            if (!rt) {
                throw std::runtime_error("QuickJS-ng: Failed to create JSRuntime");
            }

            ctx = JS_NewContext(rt);
            if (!ctx) {
                JS_FreeRuntime(rt);
                throw std::runtime_error("QuickJS-ng: Failed to create JSContext");
            }

            JS_SetModuleLoaderFunc(rt, [](JSContext* c, const char* base_name, const char* name, void* opaque) -> char* {
                size_t len = std::strlen(name);
                char* normalized = static_cast<char*>(js_malloc(c, len + 1));
                if (normalized) {
                    std::memcpy(normalized, name, len + 1);
                }
                return normalized;
            }, nullptr, nullptr);
        }

        ~Engine() {
            if (ctx) JS_FreeContext(ctx);
            if (rt) JS_FreeRuntime(rt);
        }

        // Rule of Five: QuickJS Runtimes/Contexts shouldn't be shallow copied.
        Engine(const Engine&) = delete;
        Engine& operator=(const Engine&) = delete;

        // Allow moving the engine safely
        Engine(Engine&& other) noexcept
            : rt(std::exchange(other.rt, nullptr)),
              ctx(std::exchange(other.ctx, nullptr)) {}

        Engine& operator=(Engine&& other) noexcept {
            if (this != &other) {
                if (ctx) JS_FreeContext(ctx);
                if (rt) JS_FreeRuntime(rt);
                rt = std::exchange(other.rt, nullptr);
                ctx = std::exchange(other.ctx, nullptr);
            }
            return *this;
        }

        // --- High-Level API ---

        /**
         * Evaluates a string of JavaScript code.
         */
        std::expected<std::string, std::string> eval(const std::string_view code, const std::string_view filename = "<eval>", EvalType eval_type = EvalType::Global) {
            int flags = (eval_type == EvalType::Module) ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL;
            const JSValue val = JS_Eval(ctx, code.data(), code.size(), filename.data(), flags);
            return processResult(val);
        }

        /**
         * Reads a file and evaluates its contents.
         */
        std::expected<std::string, std::string> eval_file(std::string_view filepath, EvalType eval_type = EvalType::Global) {
            std::ifstream file(filepath.data(), std::ios::in | std::ios::binary);
            if (!file) {
                return std::unexpected(std::format("Failed to open file: {}", filepath));
            }
            const std::string code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            return eval(code, filepath, eval_type);
        }

        [[nodiscard]]
        Value global() const {
            return {ctx, JS_GetGlobalObject(ctx)};
        }

        // --- Value Factories --

        Value make_value(const int32_t v) const {
            return Value(ctx, v);
        }

        Value make_value(const double v) const {
            return Value(ctx, v);
        }

        Value make_value(const bool v) const {
            return Value(ctx, v);
        }

        Value make_value(const std::string_view v) const {
            return Value(ctx, v);
        }

        Value make_value(const char* v) const {
            return Value(ctx, v);
        }

        Value make_object() const {
            return Value::create_object(ctx);
        }

        Value make_array() const {
            return Value::create_array(ctx);
        }

        ModuleBuilder make_module(const std::string_view name) const {
            return ModuleBuilder(ctx, name);
        }

        template <typename T>
        [[nodiscard]] ClassBuilder<T> make_class(const std::string_view name) {
            JSClassID active_id = 0;
            const char* class_name_ptr = nullptr;

            {
                std::unique_lock lock(detail::JSClassInfo<T>::mutex);
                if (detail::JSClassInfo<T>::ids.find(rt) == detail::JSClassInfo<T>::ids.end()) {
                    JSClassID id = 0;
                    JS_NewClassID(rt, &id);
                    detail::JSClassInfo<T>::ids[rt] = id;
                    detail::JSClassInfo<T>::name = name;
                }
                active_id = detail::JSClassInfo<T>::ids[rt];
                class_name_ptr = detail::JSClassInfo<T>::name.c_str();
            }

            // Register the class inside this specific runtime only once
            if (registered_classes.find(active_id) == registered_classes.end()) {
                JSClassDef def{};
                def.class_name = class_name_ptr;

                // Set up the finalizer to automatically delete the mapped C++ object
                def.finalizer = [](JSRuntime* runtime_ptr, JSValue val) {
                    JSClassID class_id = 0;
                    {
                        std::shared_lock lock(detail::JSClassInfo<T>::mutex);
                        if (detail::JSClassInfo<T>::ids.contains(runtime_ptr)) {
                            class_id = detail::JSClassInfo<T>::ids.at(runtime_ptr);
                        }
                    }

                    if (class_id != 0) {
                        T* ptr = static_cast<T*>(JS_GetOpaque(val, class_id));
                        delete ptr; // Safely release C++ memory
                    }
                };

                JS_NewClass(rt, active_id, &def);
                registered_classes.insert(active_id);

                {
                    std::unique_lock lock(detail::JSClassInfo<T>::mutex);
                    detail::JSClassInfo<T>::constructors[rt].clear();
                }
            }

            return ClassBuilder<T>(ctx, name);
        }

        std::expected<std::vector<uint8_t>, std::string> compile_to_bytecode(const std::string_view code, const std::string_view filename = "<eval>", EvalType eval_type = EvalType::Global) const {
            // Set up our evaluation flags, making sure to include the COMPILE_ONLY flag
            int flags = (eval_type == EvalType::Module) ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL;
            flags |= JS_EVAL_FLAG_COMPILE_ONLY;

            // Ask QuickJS to compile the code into a function object
            JSValue obj = JS_Eval(ctx, code.data(), code.size(), filename.data(), flags);

            // Check if a syntax error or other exception occurred during compilation
            if (JS_IsException(obj)) {
                JSValue exception_val = JS_GetException(ctx);

                // Extract the error message and stack trace
                const char* cstr = JS_ToCString(ctx, exception_val);
                std::string err_msg = cstr ? cstr : "Unknown compilation error";
                if (cstr) JS_FreeCString(ctx, cstr);

                if (JS_IsError(exception_val)) {
                    JSValue stack = JS_GetPropertyStr(ctx, exception_val, "stack");
                    if (!JS_IsUndefined(stack)) {
                        const char* stack_cstr = JS_ToCString(ctx, stack);
                        if (stack_cstr) {
                            err_msg += "\n" + std::string(stack_cstr);
                            JS_FreeCString(ctx, stack_cstr);
                        }
                    }
                    JS_FreeValue(ctx, stack);
                }

                JS_FreeValue(ctx, exception_val);
                JS_FreeValue(ctx, obj);
                return std::unexpected(err_msg);
            }

            // Serialize the function object into bytecode
            size_t out_buf_len = 0;
            // JS_WRITE_OBJ_BYTECODE tells QuickJS to output executable bytecode
            uint8_t* out_buf = JS_WriteObject(ctx, &out_buf_len, obj, JS_WRITE_OBJ_BYTECODE);

            // We no longer need the compiled function object, free it
            JS_FreeValue(ctx, obj);

            if (!out_buf) {
                return std::unexpected("Failed to generate bytecode: JS_WriteObject returned null");
            }

            // Copy the bytecode from the QuickJS C-buffer into a modern C++ vector
            std::vector<uint8_t> bytecode(out_buf, out_buf + out_buf_len);

            // Free the C-buffer allocated by QuickJS
            js_free(ctx, out_buf);

            return bytecode;
        }

        std::expected<std::vector<uint8_t>, std::string> compile_file_to_bytecode(std::string_view filepath, EvalType eval_type = EvalType::Global) const {
            // 1. Open the file in binary read mode
            std::ifstream file(filepath.data(), std::ios::in | std::ios::binary);
            if (!file) {
                return std::unexpected(std::format("Failed to open file: {}", filepath));
            }

            // 2. Read the entire file content into a std::string
            const std::string code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            // 3. Pass the code and the filepath to our core compilation function
            return compile_to_bytecode(code, filepath, eval_type);
        }

        /**
         * Loads and evaluates compiled QuickJS bytecode from memory.
         * If the bytecode is a module, this automatically registers it, making it
         * available for other scripts to import via the filename used during compilation.
         * * @param bytecode The vector of raw bytes.
         * @return The evaluation result as a string, or an error message.
         */
        std::expected<std::string, std::string> eval_bytecode(const std::vector<uint8_t>& bytecode) const {
            // 1. Deserialize the bytecode into a QuickJS object
            // JS_READ_OBJ_BYTECODE tells the engine we are reading executable bytecode
            JSValue obj = JS_ReadObject(ctx, bytecode.data(), bytecode.size(), JS_READ_OBJ_BYTECODE);

            // If the bytecode is invalid or corrupt, extract the error and exit
            if (JS_IsException(obj)) {
                return processResult(obj);
            }

            // 2. Evaluate the reconstructed function/module
            // Note: JS_EvalFunction automatically consumes (frees) the 'obj' parameter,
            // so we don't need to call JS_FreeValue(ctx, obj) ourselves!
            JSValue val = JS_EvalFunction(ctx, obj);

            // 3. Return the result safely using your existing helper
            return processResult(val);
        }

        /**
         * Reads a bytecode file from disk and evaluates it.
         * @param filepath The path to the binary (.bin) file.
         */
        std::expected<std::string, std::string> eval_bytecode_file(std::string_view filepath) const {
            // Open in binary mode
            std::ifstream file(filepath.data(), std::ios::in | std::ios::binary);
            if (!file) {
                return std::unexpected(std::format("Failed to open bytecode file: {}", filepath));
            }

            // Efficiently read the entire binary file into a vector
            file.seekg(0, std::ios::end);
            size_t size = file.tellg();
            file.seekg(0, std::ios::beg);

            std::vector<uint8_t> bytecode(size);
            if (size > 0) {
                file.read(reinterpret_cast<char*>(bytecode.data()), size);
            }

            // Pass the loaded bytes to our core bytecode evaluator
            return eval_bytecode(bytecode);
        }

        void run_gc() const {
            JS_RunGC(rt);
        }

    private:
        JSRuntime* rt{nullptr};
        JSContext* ctx{nullptr};

        std::unordered_set<JSClassID> registered_classes;

        // Helper to extract the result or error, and safely free the JSValues
        std::expected<std::string, std::string> processResult(const JSValue val) const {
            auto toStdString = [this](const JSValue v) -> std::string {
                const char* cstr = JS_ToCString(ctx, v);
                if (!cstr) return "";
                std::string str(cstr);
                JS_FreeCString(ctx, cstr);
                return str;
            };

            if (JS_IsException(val)) {
                const JSValue exception_val = JS_GetException(ctx);
                std::string err_msg = toStdString(exception_val);

                // Check if it's a JS Error object to extract the stack trace
                if (JS_IsError(exception_val)) {
                    const JSValue stack = JS_GetPropertyStr(ctx, exception_val, "stack");
                    if (!JS_IsUndefined(stack)) {
                        err_msg += "\n" + toStdString(stack);
                    }
                    JS_FreeValue(ctx, stack);
                }

                JS_FreeValue(ctx, exception_val);
                JS_FreeValue(ctx, val);
                return std::unexpected(err_msg);
            }

            // Success state
            std::string result = toStdString(val);
            JS_FreeValue(ctx, val);
            return result;
        }
    };

} // namespace qjs