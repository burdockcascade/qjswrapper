#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <expected>
#include <fstream>
#include <filesystem>

#include <quickjs.h>
#include "object.hpp"
#include "class.hpp"
#include "util.hpp"
#include "module.hpp"

namespace qjs {

    enum class EvalMode {
        Script, // Runs in the global context (variables become global)
        Module  // Runs as an ES6 module (supports import/export, strict mode by default)
    };

    struct EngineError {
        std::string message;
        std::string filename;
        int line_number = -1;
        std::string stack;

        [[nodiscard]] std::string to_string() const {
            std::string out = message;
            if (!stack.empty()) {
                out += "\nStack:\n" + stack;
            }
            return out;
        }
    };

    class Engine {

    public:
        Engine() : rt(JS_NewRuntime()), ctx(JS_NewContext(rt.get())), global_wrapper(Value(ctx.get(), JS_GetGlobalObject(ctx.get()))) {
            JS_SetContextOpaque(ctx.get(), this);

            JS_SetHostPromiseRejectionTracker(rt.get(), promise_rejection_tracker, this);

            JS_SetModuleLoaderFunc(rt.get(), nullptr, [](JSContext* ctx, const char* module_name, void* opaque) -> JSModuleDef* {
                Engine* eng = static_cast<Engine*>(opaque);
                std::string name_str(module_name);

                // 1. Check Native C++ modules
                for (const auto& mod : eng->modules_) {
                    if (mod->name == name_str) return mod->js_module;
                }

                // 2. Check pre-registered JS Source modules
                auto s_it = eng->source_modules_.find(name_str);
                if (s_it != eng->source_modules_.end()) {
                    JSValue func_val = JS_Eval(ctx, s_it->second.c_str(), s_it->second.size(),
                                               module_name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
                    if (JS_IsException(func_val)) return nullptr;
                    auto* m = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(func_val));
                    JS_FreeValue(ctx, func_val);
                    return m;
                }

                // 3. Check Pre-registered Embedded Bytecode modules
                auto b_it = eng->bytecode_modules_.find(name_str);
                if (b_it != eng->bytecode_modules_.end()) {
                    JSValue obj = JS_ReadObject(ctx, b_it->second.data, b_it->second.len, JS_READ_OBJ_BYTECODE);
                    if (JS_IsException(obj)) return nullptr;
                    auto* m = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(obj));
                    JS_FreeValue(ctx, obj);
                    return m;
                }

                // 4. Fallback: Try loading as a standard JS file from the filesystem
                std::filesystem::path p(name_str);
                if (!std::filesystem::exists(p)) {
                    JS_ThrowReferenceError(ctx, "Could not find module '%s'", module_name);
                    return nullptr;
                }

                std::ifstream f(p, std::ios::binary | std::ios::ate);
                if (!f) return nullptr;

                std::streamsize size = f.tellg();
                f.seekg(0, std::ios::beg);
                std::string code(size, '\0');
                f.read(code.data(), size);

                JSValue func_val = JS_Eval(ctx, code.data(), code.size(),
                                           module_name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);

                if (JS_IsException(func_val)) return nullptr;

                auto* m = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(func_val));
                JS_FreeValue(ctx, func_val);
                return m;
            }, this);
        }

        ~Engine() {
            // Clean up any lingering rejection values
            if (!JS_IsUndefined(last_promise_rejection_)) {
                JS_FreeValue(ctx.get(), last_promise_rejection_);
            }
        }

        // --- Execution APIs ---

        std::expected<std::string, EngineError> eval(std::string_view code, std::string_view filename = "<eval>", const EvalMode mode = EvalMode::Script) const {
            int eval_flags = (mode == EvalMode::Module) ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL;
            JSValue ret = JS_Eval(ctx.get(), code.data(), code.size(), filename.data(), eval_flags);

            return handle_execution_result(ret);
        }

        std::expected<std::string, EngineError> eval_file(const std::filesystem::path& p, EvalMode mode = EvalMode::Script) const {
            std::ifstream f(p, std::ios::binary | std::ios::ate);

            if (!f) {
                return std::unexpected(EngineError{
                    .message = "File not found: " + p.string(),
                    .filename = p.string()
                });
            }

            std::streamsize size = f.tellg();
            f.seekg(0, std::ios::beg);
            std::string code(size, '\0');
            if (!f.read(code.data(), size)) {
                return std::unexpected(EngineError{
                    .message = "Failed to read file: " + p.string(),
                    .filename = p.string()
                });
            }

            return eval(code, p.string(), mode);
        }

        std::expected<std::string, EngineError> eval_bytecode(const uint8_t* bytecode, size_t len) const {
            const JSValue obj = JS_ReadObject(ctx.get(), bytecode, len, JS_READ_OBJ_BYTECODE);
            if (JS_IsException(obj)) return wrap_result(obj);

            JSValue ret = JS_EvalFunction(ctx.get(), obj);
            return handle_execution_result(ret);
        }

        // --- Loading/Registration APIs ---

        void register_module_source(const std::string& name, std::string_view code) {
            source_modules_[name] = std::string(code);
        }

        void register_module_bytecode(const std::string& name, const uint8_t* bytecode, size_t len) {
            bytecode_modules_[name] = { bytecode, len };
        }

        // --- Compilation APIs ---

        std::expected<std::vector<uint8_t>, EngineError> compile_file_to_bytecode(const std::filesystem::path& p, EvalMode mode = EvalMode::Module) const {
            std::ifstream f(p, std::ios::binary | std::ios::ate);

            if (!f) {
                return std::unexpected(EngineError{
                    .message = "File not found: " + p.string(),
                    .filename = p.string()
                });
            }

            std::streamsize size = f.tellg();
            f.seekg(0, std::ios::beg);
            std::string code(size, '\0');
            if (!f.read(code.data(), size)) {
                return std::unexpected(EngineError{
                    .message = "Failed to read the file",
                    .filename = p.string()
                });
            }

            int eval_flags = JS_EVAL_FLAG_COMPILE_ONLY;
            eval_flags |= (mode == EvalMode::Module) ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL;

            JSValue func_val = JS_Eval(ctx.get(), code.data(), code.size(), p.string().c_str(), eval_flags);

            if (JS_IsException(func_val)) {
                auto err = wrap_result(func_val);
                return std::unexpected(err.error());
            }

            size_t out_buf_len;
            uint8_t* out_buf = JS_WriteObject(ctx.get(), &out_buf_len, func_val, JS_WRITE_OBJ_BYTECODE);
            JS_FreeValue(ctx.get(), func_val);

            if (!out_buf) {
                return std::unexpected(EngineError{
                    .message = "Failed to serialize the bytecode",
                    .filename = p.string()
                });
            }

            std::vector<uint8_t> bytecode(out_buf, out_buf + out_buf_len);
            js_free(ctx.get(), out_buf);

            return bytecode;
        }

        // --- Make Stuff ---

        [[nodiscard]] Object make_object() const {
            return Object(Value(ctx.get(), JS_NewObject(ctx.get())));
        }

        template<typename T>
        Class<T> define_class(const std::string& name) {
            auto ctx_ptr = ctx.get();
            auto rt_ptr = rt.get();

            ClassRegistry<T>::init(rt_ptr, name);
            auto class_id = ClassRegistry<T>::class_id;

            init_wrapper_class(ctx_ptr);

            const Value proto_val(ctx_ptr, JS_NewObject(ctx_ptr));
            JS_SetClassProto(ctx_ptr, class_id, JS_DupValue(ctx_ptr, proto_val.get()));

            JSValue opaque_obj = JS_NewObjectClass(ctx_ptr, wrapper_class_id);
            auto* dispatcher = new ConstructorDispatcher<T>();
            JS_SetOpaque(opaque_obj, dispatcher);

            JSValue js_ctor = JS_NewCFunctionData(ctx_ptr,
                &ConstructorDispatcher<T>::apply,
                0, 0, 1, &opaque_obj
            );
            JS_FreeValue(ctx_ptr, opaque_obj);

            JS_SetConstructorBit(ctx_ptr, js_ctor, true);
            JS_SetConstructor(ctx_ptr, js_ctor, proto_val.get());

            JS_SetPropertyStr(ctx_ptr, JS_GetGlobalObject(ctx_ptr), name.c_str(), JS_DupValue(ctx_ptr, js_ctor));

            return Class<T>(Object(Value(ctx_ptr, js_ctor)), Object(proto_val), dispatcher);
        }

        Module define_module(const std::string& name) {
            auto m_def = std::make_unique<ModuleDef>();
            m_def->name = name;
            auto* def_ptr = m_def.get();
            modules_.push_back(std::move(m_def));

            JSModuleDef* js_module = JS_NewCModule(ctx.get(), name.c_str(), module_init_func);
            def_ptr->js_module = js_module;

            module_map_[js_module] = def_ptr;

            auto add_cb = [def_ptr](std::string exp_name, Value val) {
                def_ptr->exports.emplace_back(std::move(exp_name), std::move(val));
            };

            return Module(ctx.get(), js_module, std::move(add_cb));
        }

        [[nodiscard]] Object& global() {
            return global_wrapper;
        }

    private:
        struct RTDel { void operator()(JSRuntime* r) const { JS_FreeRuntime(r); } };
        struct CTDel { void operator()(JSContext* c) const { JS_FreeContext(c); } };
        std::unique_ptr<JSRuntime, RTDel> rt;
        std::unique_ptr<JSContext, CTDel> ctx;
        Object global_wrapper;

        // --- Module Registry Structs ---
        struct ModuleDef {
            std::string name;
            std::vector<std::pair<std::string, Value>> exports;
            JSModuleDef* js_module;
        };
        std::vector<std::unique_ptr<ModuleDef>> modules_;
        std::unordered_map<JSModuleDef*, ModuleDef*> module_map_;

        std::unordered_map<std::string, std::string> source_modules_;
        struct EmbeddedBytecode {
            const uint8_t* data;
            size_t len;
        };
        std::unordered_map<std::string, EmbeddedBytecode> bytecode_modules_;

        static int module_init_func(JSContext *ctx, JSModuleDef *m) {
            Engine* eng = static_cast<Engine*>(JS_GetContextOpaque(ctx));
            if (!eng) return -1;
            return eng->init_module_internal(m);
        }

        int init_module_internal(JSModuleDef* m) {
            auto it = module_map_.find(m);
            if (it == module_map_.end()) return -1;

            for (auto& [name, val] : it->second->exports) {
                JS_SetModuleExport(ctx.get(), m, name.c_str(), JS_DupValue(ctx.get(), val.get()));
            }
            return 0;
        }

        mutable JSValue last_promise_rejection_ = JS_UNDEFINED;

        static void promise_rejection_tracker(JSContext *ctx, JSValueConst /*promise*/, JSValueConst reason, bool is_handled, void *opaque) {
            Engine* eng = static_cast<Engine*>(opaque);
            if (!is_handled) {
                if (!JS_IsUndefined(eng->last_promise_rejection_)) {
                    JS_FreeValue(ctx, eng->last_promise_rejection_);
                }
                eng->last_promise_rejection_ = JS_DupValue(ctx, reason);
            } else {
                if (!JS_IsUndefined(eng->last_promise_rejection_)) {
                    JS_FreeValue(ctx, eng->last_promise_rejection_);
                    eng->last_promise_rejection_ = JS_UNDEFINED;
                }
            }
        }

        std::expected<std::string, EngineError> handle_execution_result(JSValue ret) const {
            if (JS_IsException(ret)) return wrap_result(ret);

            JSContext* pctx;
            int err;
            while ((err = JS_ExecutePendingJob(rt.get(), &pctx)) > 0) {}

            if (err < 0) {
                JSValue exception = JS_GetException(pctx);
                JS_FreeValue(ctx.get(), ret);

                // The event loop failed. Throw the extracted error object back.
                JS_Throw(ctx.get(), exception);
                return wrap_result(JS_EXCEPTION);
            }

            if (!JS_IsUndefined(last_promise_rejection_)) {
                JSValue rejected = JS_DupValue(ctx.get(), last_promise_rejection_);

                JS_FreeValue(ctx.get(), last_promise_rejection_);
                last_promise_rejection_ = JS_UNDEFINED;
                JS_FreeValue(ctx.get(), ret);

                // The module's promise rejected. Throw the rejected value back.
                JS_Throw(ctx.get(), rejected);
                return wrap_result(JS_EXCEPTION);
            }

            return wrap_result(ret);
        }

        std::expected<std::string, EngineError> wrap_result(const JSValue v) const {
            const Value managed_val(ctx.get(), v);

            if (JS_IsException(managed_val.get())) {
                JSValue exception = JS_GetException(ctx.get());
                EngineError err;

                err.message = converter<std::string>::get(ctx.get(), exception);

                if (JS_IsError(exception)) {
                    JSValue stack = JS_GetPropertyStr(ctx.get(), exception, "stack");
                    if (!JS_IsUndefined(stack)) {
                        err.stack = converter<std::string>::get(ctx.get(), stack);
                        JS_FreeValue(ctx.get(), stack);
                    }
                }

                JS_FreeValue(ctx.get(), exception);
                return std::unexpected(err);
            }

            return converter<std::string>::get(ctx.get(), managed_val.get());
        }

    };

} // namespace qjs