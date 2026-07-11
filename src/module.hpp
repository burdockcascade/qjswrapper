#pragma once

#include <quickjs.h>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <unordered_map>
#include <utility>
#include <mutex>

#include "value.hpp"

namespace qjs {

    namespace detail {
        // QuickJS native modules use a C callback for initialization.
        // We map the module definition pointer to our captured C++ lambdas and values.
        struct ModuleRegistry {
            static inline std::mutex mutex;
            static inline std::unordered_map<JSModuleDef*, std::vector<std::function<void(JSContext*, JSModuleDef*)>>> inits;

            static int init_trampoline(JSContext* ctx, JSModuleDef* m) {
                std::vector<std::function<void(JSContext*, JSModuleDef*)>> actions;

                {
                    std::unique_lock lock(mutex);
                    auto it = inits.find(m);
                    if (it != inits.end()) {
                        actions = std::move(it->second);
                        inits.erase(it);
                    }
                }

                // Execute all bound exports (values, functions, etc.)
                // This is done outside the lock to prevent deadlocks if an action triggers another JS evaluation.
                for (const auto& action : actions) {
                    action(ctx, m);
                }

                return 0;
            }
        };
    }

    class ModuleBuilder {
    public:
        ModuleBuilder(JSContext* ctx, const std::string_view name) : ctx(ctx), name(name) {}

        ModuleBuilder(const ModuleBuilder&) = delete;
        ModuleBuilder& operator=(const ModuleBuilder&) = delete;

        ModuleBuilder(ModuleBuilder&& other) noexcept
            : ctx(std::exchange(other.ctx, nullptr)),
              name(std::move(other.name)),
              exports(std::move(other.exports)),
              init_actions(std::move(other.init_actions)) {}

        ~ModuleBuilder() {
            if (!ctx) return;

            JSModuleDef* m = JS_NewCModule(ctx, name.c_str(), detail::ModuleRegistry::init_trampoline);
            if (!m) return;

            {
                std::unique_lock lock(detail::ModuleRegistry::mutex);
                detail::ModuleRegistry::inits[m] = std::move(init_actions);
            }

            for (const auto& exp : exports) {
                JS_AddModuleExport(ctx, m, exp.c_str());
            }
        }

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

                // FIX: Graceful handling of QuickJS memory allocation exception
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

} // namespace qjs