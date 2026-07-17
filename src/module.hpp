#pragma once

#include <quickjs.h>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <unordered_map>
#include <utility>
#include <mutex>
#include <stdexcept>

#include "value.hpp"

namespace qjswrapper {

    namespace detail {
        struct ModuleRegistry {
            static inline std::mutex mutex;
            static inline std::unordered_map<JSModuleDef*, std::vector<std::function<void(JSContext*, JSModuleDef*)>>> inits;

            static int init_trampoline(JSContext* ctx, JSModuleDef* m);
        };
    }

    class ModuleBuilder {
    public:
        ModuleBuilder(JSContext* ctx, const std::string_view name);

        ModuleBuilder(const ModuleBuilder&) = delete;
        ModuleBuilder& operator=(const ModuleBuilder&) = delete;

        ModuleBuilder(ModuleBuilder&& other) noexcept;

        ~ModuleBuilder();

        template <typename T>
        ModuleBuilder& add_value(const std::string_view prop_name, T&& v) {
            std::string name_str(prop_name);
            exports.push_back(name_str);

            using Decayed = std::decay_t<T>;
            init_actions.push_back([name_str, val = Decayed(std::forward<T>(v))](JSContext* c, JSModuleDef* m) mutable {
                JSValue js_val = Value::create_js_value(c, std::move(val));
                JS_SetModuleExport(c, m, name_str.c_str(), js_val);
            });

            return *this;
        }

        template <typename F>
        ModuleBuilder& add_function(const std::string_view func_name, F&& func) {
            std::string name_str(func_name);
            exports.push_back(name_str);

            using DecayedF = std::decay_t<F>;
            using Traits = detail::function_traits<DecayedF>;

            init_actions.push_back([name_str, f = DecayedF(std::forward<F>(func))](JSContext* c, JSModuleDef* m) mutable {
                auto* func_ptr = new DecayedF(std::move(f));

                JSValue data_obj = JS_NewArrayBuffer(
                    c, reinterpret_cast<uint8_t*>(func_ptr), sizeof(DecayedF),
                    detail::lambda_free_func<DecayedF>, nullptr, false
                );

                if (JS_IsException(data_obj)) {
                    delete func_ptr;
                    throw std::runtime_error("ModuleBuilder: Failed to allocate ArrayBuffer for function export.");
                }

                JSValue js_func = JS_NewCFunctionData(
                    c, detail::cpp_closure_trampoline<DecayedF>, Traits::arity, 0, 1, &data_obj
                );

                JS_SetModuleExport(c, m, name_str.c_str(), js_func);
                JS_FreeValue(c, data_obj);
            });

            return *this;
        }

    private:
        JSContext* ctx{nullptr};
        std::string name;
        std::vector<std::string> exports;
        std::vector<std::function<void(JSContext*, JSModuleDef*)>> init_actions;
    };

} // namespace qjswrapper