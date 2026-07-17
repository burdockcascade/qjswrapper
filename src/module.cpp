#include "module.hpp"

namespace qjswrapper {

    namespace detail {
        int ModuleRegistry::init_trampoline(JSContext* ctx, JSModuleDef* m) {
            std::vector<std::function<void(JSContext*, JSModuleDef*)>> actions;

            {
                std::unique_lock lock(mutex);
                if (auto it = inits.find(m); it != inits.end()) {
                    actions = std::move(it->second);
                    inits.erase(it);
                }
            }

            for (const auto& action : actions) {
                action(ctx, m);
            }

            return 0;
        }
    }

    ModuleBuilder::ModuleBuilder(JSContext* ctx, const std::string_view name)
        : ctx(ctx), name(name) {}

    ModuleBuilder::ModuleBuilder(ModuleBuilder&& other) noexcept
        : ctx(std::exchange(other.ctx, nullptr)),
          name(std::move(other.name)),
          exports(std::move(other.exports)),
          init_actions(std::move(other.init_actions)) {}

    ModuleBuilder::~ModuleBuilder() {
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

} // namespace qjswrapper