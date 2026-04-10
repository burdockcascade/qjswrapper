#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <expected>
#include <fstream>
#include <sstream>
#include <filesystem>

#include <quickjs.h>
#include "object.hpp"
#include "class.hpp"
#include "util.hpp"
#include "module.hpp"

namespace qjs {

    class Engine {

    public:
        Engine() : rt(JS_NewRuntime()), ctx(JS_NewContext(rt.get())), global_wrapper(Value(ctx.get(), JS_GetGlobalObject(ctx.get()))) {
            JS_SetContextOpaque(ctx.get(), this);

            // Updated Module Loader: Handles C++ Native, Embedded Bytecode, and Source files
            JS_SetModuleLoaderFunc(rt.get(), nullptr, [](JSContext* ctx, const char* module_name, void* opaque) -> JSModuleDef* {
                Engine* eng = static_cast<Engine*>(opaque);
                std::string name_str(module_name);

                // 1. Check Native C++ modules
                for (const auto& mod : eng->modules_) {
                    if (mod->name == name_str) {
                        return mod->js_module;
                    }
                }

                // 2. Check Embedded Bytecode modules
                auto b_it = eng->bytecode_modules_.find(name_str);
                if (b_it != eng->bytecode_modules_.end()) {
                    // Read the precompiled bytecode directly from memory
                    JSValue obj = JS_ReadObject(ctx, b_it->second.data, b_it->second.len, JS_READ_OBJ_BYTECODE);
                    if (JS_IsException(obj)) {
                        return nullptr;
                    }

                    // Extract the module definition pointer and free the wrapper value
                    auto* m = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(obj));
                    JS_FreeValue(ctx, obj);
                    return m;
                }

                // 3. Fallback: Try loading as a standard JS file from the filesystem
                std::filesystem::path p(name_str);
                if (!std::filesystem::exists(p)) {
                    JS_ThrowReferenceError(ctx, "Could not find module '%s'", module_name);
                    return nullptr;
                }

                std::ifstream f(p, std::ios::binary | std::ios::ate);
                if (!f) return nullptr;

                std::streamsize size = f.tellg();
                f.seekg(0, std::ios::beg);
                std::vector<uint8_t> buffer(size);
                f.read(reinterpret_cast<char*>(buffer.data()), size);

                // Compile source file into a module (but do not evaluate it yet)
                JSValue func_val = JS_Eval(ctx, reinterpret_cast<const char*>(buffer.data()), buffer.size(),
                                           module_name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);

                if (JS_IsException(func_val)) return nullptr;

                auto* m = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(func_val));
                JS_FreeValue(ctx, func_val);
                return m;
            }, this);
        }

        ~Engine() = default;

        // Script execution
        std::expected<std::string, std::string> eval_global(const std::string_view code, std::string_view filename) const {
            return wrap_result(JS_Eval(ctx.get(), code.data(), code.size(), filename.data(), JS_EVAL_TYPE_GLOBAL));
        }

        std::expected<std::string, std::string> eval_module(const std::string_view code, std::string_view filename) const {
            JSValue ret = JS_Eval(ctx.get(), code.data(), code.size(), filename.data(), JS_EVAL_TYPE_MODULE);

            // Catch immediate syntax errors
            if (JS_IsException(ret)) {
                return wrap_result(ret);
            }

            // 2. Spin the Event Loop! Modules run as Promises, so we must execute pending jobs.
            JSContext* pctx;
            int err;
            while ((err = JS_ExecutePendingJob(rt.get(), &pctx)) > 0) {
                // Keep looping until the job queue is completely empty
            }

            // If a runtime error happened during the job, catch it
            if (err < 0) {
                JSValue exception = JS_GetException(pctx);
                JS_FreeValue(ctx.get(), ret);  // Clean up the original promise
                return wrap_result(exception); // Wrap and return the job error
            }

            return wrap_result(ret);
        }

        std::expected<std::string, std::string> run_file(const std::filesystem::path& p) const {
            const std::ifstream f(p);
            if (!f) return std::unexpected("File not found: " + p.string());
            std::stringstream b;
            b << f.rdbuf();
            return eval_global(b.str(), p.filename().string());
        }

        std::expected<std::string, std::string> run_bytecode(const uint8_t* bytecode, size_t len) const {
            const JSValue obj = JS_ReadObject(ctx.get(), bytecode, len, JS_READ_OBJ_BYTECODE);
            if (JS_IsException(obj)) return wrap_result(obj);
            return wrap_result(JS_EvalFunction(ctx.get(), obj));
        }

        void register_bytecode_module(const std::string& name, const uint8_t* bytecode, size_t len) {
            bytecode_modules_[name] = { bytecode, len };
        }

        // Compiles a JavaScript file into QuickJS bytecode and returns it
        // The as_module flag determines if it should be compiled as an ES6 module
        std::expected<std::vector<uint8_t>, std::string> compile_file_to_bytecode(const std::filesystem::path& p, const bool as_module = true) const {
            // 1. Read the JS source file
            std::ifstream f(p, std::ios::binary | std::ios::ate);
            if (!f) return std::unexpected("File not found: " + p.string());

            std::streamsize size = f.tellg();
            f.seekg(0, std::ios::beg);

            std::string code(size, '\0');
            if (!f.read(code.data(), size)) {
                return std::unexpected("Failed to read file: " + p.string());
            }

            // 2. Determine compilation flags
            int eval_flags = JS_EVAL_FLAG_COMPILE_ONLY;
            eval_flags |= as_module ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL;

            // 3. Compile the JS code into a QuickJS function object
            JSValue func_val = JS_Eval(ctx.get(), code.data(), code.size(), p.string().c_str(), eval_flags);

            // Handle syntax errors during compilation
            if (JS_IsException(func_val)) {
                auto err = wrap_result(func_val);
                // wrap_result returns unexpected on exception, so we extract the error string
                return std::unexpected(err.error());
            }

            // 4. Serialize the compiled function object to a raw bytecode buffer
            size_t out_buf_len;
            uint8_t* out_buf = JS_WriteObject(ctx.get(), &out_buf_len, func_val, JS_WRITE_OBJ_BYTECODE);

            // Free the function object as we no longer need it
            JS_FreeValue(ctx.get(), func_val);

            if (!out_buf) {
                return std::unexpected("Failed to serialize bytecode (Out of memory or unsupported feature)");
            }

            // 5. Copy to a std::vector to manage the memory safely in C++
            std::vector<uint8_t> bytecode(out_buf, out_buf + out_buf_len);

            // Free the QuickJS-allocated buffer
            js_free(ctx.get(), out_buf);

            return bytecode;
        }

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

        // Define a new Native Module
        Module define_module(const std::string& name) {
            auto m_def = std::make_unique<ModuleDef>();
            m_def->name = name;
            auto* def_ptr = m_def.get();
            modules_.push_back(std::move(m_def));

            JSModuleDef* js_module = JS_NewCModule(ctx.get(), name.c_str(), module_init_func);
            def_ptr->js_module = js_module; // <-- Save the module pointer!

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
            JSModuleDef* js_module; // Added to map back in the loader
        };
        std::vector<std::unique_ptr<ModuleDef>> modules_;
        std::unordered_map<JSModuleDef*, ModuleDef*> module_map_;

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

        std::expected<std::string, std::string> wrap_result(const JSValue v) const {
            const Value managed_val(ctx.get(), v);

            if (JS_IsException(managed_val.get())) {
                const JSValue exception = JS_GetException(ctx.get());
                const Value managed_exc(ctx.get(), exception);
                return std::unexpected(converter<std::string>::get(ctx.get(), managed_exc.get()));
            }

            return converter<std::string>::get(ctx.get(), managed_val.get());
        }

        struct EmbeddedBytecode {
            const uint8_t* data;
            size_t len;
        };
        std::unordered_map<std::string, EmbeddedBytecode> bytecode_modules_;

    };

} // namespace qjs