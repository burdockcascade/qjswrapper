#pragma once

#include <quickjs.h>
#include <expected>
#include <string>
#include <string_view>
#include <vector>
#include <utility>
#include <unordered_set>
#include <shared_mutex>
#include <mutex>

#include "value.hpp"
#include "class.hpp"
#include "module.hpp"

namespace qjswrapper {

    enum class EvalType {
        Global,
        Module
    };

    class Engine {
    public:
        Engine();
        ~Engine();

        // Rule of Five: QuickJS Runtimes/Contexts shouldn't be shallow copied.
        Engine(const Engine&) = delete;
        Engine& operator=(const Engine&) = delete;

        // Allow moving the engine safely
        Engine(Engine&& other) noexcept;
        Engine& operator=(Engine&& other) noexcept;

        // --- High-Level API ---
        std::expected<std::string, std::string> eval(const std::string_view code, const std::string_view filename = "<eval>", EvalType eval_type = EvalType::Global);
        std::expected<std::string, std::string> eval_file(std::string_view filepath, EvalType eval_type = EvalType::Global);

        [[nodiscard]] Value global() const;

        // --- Value Factories --
        Value make_value(const int32_t v) const;
        Value make_value(const double v) const;
        Value make_value(const bool v) const;
        Value make_value(const std::string_view v) const;
        Value make_value(const char* v) const;
        Value make_object() const;
        Value make_array() const;

        ModuleBuilder make_module(const std::string_view name) const;

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

        std::expected<std::vector<uint8_t>, std::string> compile_to_bytecode(const std::string_view code, const std::string_view filename = "<eval>", EvalType eval_type = EvalType::Global) const;
        std::expected<std::vector<uint8_t>, std::string> compile_file_to_bytecode(std::string_view filepath, EvalType eval_type = EvalType::Global) const;
        std::expected<std::string, std::string> eval_bytecode(const std::vector<uint8_t>& bytecode) const;
        std::expected<std::string, std::string> eval_bytecode_file(std::string_view filepath) const;

        void run_gc() const;

    private:
        JSRuntime* rt{nullptr};
        JSContext* ctx{nullptr};
        std::unordered_set<JSClassID> registered_classes;

        std::expected<std::string, std::string> processResult(const JSValue val) const;
    };

} // namespace qjswrapper