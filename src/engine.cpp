#include "engine.hpp"
#include <fstream>
#include <format>
#include <stdexcept>
#include <cstring>

namespace qjswrapper {

    Engine::Engine() {
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

    Engine::~Engine() {
        if (ctx) JS_FreeContext(ctx);
        if (rt) JS_FreeRuntime(rt);
    }

    Engine::Engine(Engine&& other) noexcept
        : rt(std::exchange(other.rt, nullptr)),
          ctx(std::exchange(other.ctx, nullptr)),
          registered_classes(std::move(other.registered_classes)) {}

    Engine& Engine::operator=(Engine&& other) noexcept {
        if (this != &other) {
            if (ctx) JS_FreeContext(ctx);
            if (rt) JS_FreeRuntime(rt);
            rt = std::exchange(other.rt, nullptr);
            ctx = std::exchange(other.ctx, nullptr);
            registered_classes = std::move(other.registered_classes);
        }
        return *this;
    }

    std::expected<std::string, std::string> Engine::eval(const std::string_view code, const std::string_view filename, EvalType eval_type) {
        int flags = (eval_type == EvalType::Module) ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL;
        const JSValue val = JS_Eval(ctx, code.data(), code.size(), filename.data(), flags);
        return processResult(val);
    }

    std::expected<std::string, std::string> Engine::eval_file(std::string_view filepath, EvalType eval_type) {
        std::ifstream file(filepath.data(), std::ios::in | std::ios::binary);
        if (!file) {
            return std::unexpected(std::format("Failed to open file: {}", filepath));
        }
        const std::string code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return eval(code, filepath, eval_type);
    }

    Value Engine::global() const {
        return {ctx, JS_GetGlobalObject(ctx)};
    }

    Value Engine::make_value(const int32_t v) const { return Value(ctx, v); }
    Value Engine::make_value(const double v) const  { return Value(ctx, v); }
    Value Engine::make_value(const bool v) const    { return Value(ctx, v); }
    Value Engine::make_value(const std::string_view v) const { return Value(ctx, v); }
    Value Engine::make_value(const char* v) const   { return Value(ctx, v); }

    Value Engine::make_object() const { return Value::create_object(ctx); }
    Value Engine::make_array() const  { return Value::create_array(ctx); }

    ModuleBuilder Engine::make_module(const std::string_view name) const {
        return ModuleBuilder(ctx, name);
    }

    std::expected<std::vector<uint8_t>, std::string> Engine::compile_to_bytecode(const std::string_view code, const std::string_view filename, EvalType eval_type) const {
        int flags = (eval_type == EvalType::Module) ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL;
        flags |= JS_EVAL_FLAG_COMPILE_ONLY;

        JSValue obj = JS_Eval(ctx, code.data(), code.size(), filename.data(), flags);

        if (JS_IsException(obj)) {
            JSValue exception_val = JS_GetException(ctx);

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

        size_t out_buf_len = 0;
        uint8_t* out_buf = JS_WriteObject(ctx, &out_buf_len, obj, JS_WRITE_OBJ_BYTECODE);
        JS_FreeValue(ctx, obj);

        if (!out_buf) {
            return std::unexpected("Failed to generate bytecode: JS_WriteObject returned null");
        }

        std::vector<uint8_t> bytecode(out_buf, out_buf + out_buf_len);
        js_free(ctx, out_buf);

        return bytecode;
    }

    std::expected<std::vector<uint8_t>, std::string> Engine::compile_file_to_bytecode(std::string_view filepath, EvalType eval_type) const {
        std::ifstream file(filepath.data(), std::ios::in | std::ios::binary);
        if (!file) {
            return std::unexpected(std::format("Failed to open file: {}", filepath));
        }
        const std::string code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return compile_to_bytecode(code, filepath, eval_type);
    }

    std::expected<std::string, std::string> Engine::eval_bytecode(const std::vector<uint8_t>& bytecode) const {
        JSValue obj = JS_ReadObject(ctx, bytecode.data(), bytecode.size(), JS_READ_OBJ_BYTECODE);
        if (JS_IsException(obj)) {
            return processResult(obj);
        }
        JSValue val = JS_EvalFunction(ctx, obj);
        return processResult(val);
    }

    std::expected<std::string, std::string> Engine::eval_bytecode_file(std::string_view filepath) const {
        std::ifstream file(filepath.data(), std::ios::in | std::ios::binary);
        if (!file) {
            return std::unexpected(std::format("Failed to open bytecode file: {}", filepath));
        }

        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> bytecode(size);
        if (size > 0) {
            file.read(reinterpret_cast<char*>(bytecode.data()), size);
        }

        return eval_bytecode(bytecode);
    }

    void Engine::run_gc() const {
        JS_RunGC(rt);
    }

    std::expected<std::string, std::string> Engine::processResult(const JSValue val) const {
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

        std::string result = toStdString(val);
        JS_FreeValue(ctx, val);
        return result;
    }

} // namespace qjswrapper