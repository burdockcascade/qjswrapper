#pragma once

#include <quickjs.h>
#include <string>
#include <string_view>
#include <utility>
#include <optional>
#include <vector>
#include <concepts>
#include <type_traits>
#include <tuple>
#include <stdexcept>
#include <unordered_map>
#include <functional>
#include <shared_mutex>
#include <mutex>

namespace qjswrapper {

    namespace detail {
        // Thread-safe class info registry
        template <typename T>
        struct JSClassInfo {
            static inline std::shared_mutex mutex;
            static inline std::unordered_map<JSRuntime*, JSClassID> ids;
            static inline std::string name;
            static inline std::unordered_map<JSRuntime*, std::vector<std::function<T*(JSContext*, int, JSValueConst*)>>> constructors;
        };

        // Function Traits: Deduces Return Types and Arguments from Callables
        template <typename T>
        struct function_traits : public function_traits<decltype(&T::operator())> {};

        template <typename ClassType, typename ReturnType, typename... Args>
        struct function_traits<ReturnType(ClassType::*)(Args...) const> {
            using result_type = ReturnType;
            using args_tuple = std::tuple<std::decay_t<Args>...>;
            static constexpr std::size_t arity = sizeof...(Args);
        };

        template <typename ClassType, typename ReturnType, typename... Args>
        struct function_traits<ReturnType(ClassType::*)(Args...)> {
            using result_type = ReturnType;
            using args_tuple = std::tuple<std::decay_t<Args>...>;
            static constexpr std::size_t arity = sizeof...(Args);
        };

        template <typename ReturnType, typename... Args>
        struct function_traits<ReturnType(*)(Args...)> {
            using result_type = ReturnType;
            using args_tuple = std::tuple<std::decay_t<Args>...>;
            static constexpr std::size_t arity = sizeof...(Args);
        };

        template <typename F>
        void lambda_free_func(JSRuntime* rt, void* opaque, void* ptr) {
            delete static_cast<F*>(ptr);
        }

        template <typename F>
        JSValue cpp_closure_trampoline(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* func_data);
    }

    // Broadened to accept any integral, floating point, string, or class
    template <typename T>
    concept JSSupportedType = std::is_integral_v<std::decay_t<T>> ||
                              std::is_floating_point_v<std::decay_t<T>> ||
                              std::is_same_v<std::decay_t<T>, std::string> ||
                              std::convertible_to<std::decay_t<T>, std::string_view> ||
                              std::is_class_v<std::decay_t<T>>;

    class Value {
    public:
        Value() = default;
        Value(JSContext* ctx, const JSValue val);

        template <JSSupportedType T>
        Value(JSContext* ctx, T&& v) : ctx(ctx) {
            val = create_js_value(ctx, std::forward<T>(v));
        }

        ~Value();

        Value(const Value& other);
        Value& operator=(const Value& other);
        Value(Value&& other) noexcept;
        Value& operator=(Value&& other) noexcept;

        // Converts native C++ types OR existing Values into QuickJS JSValues
        template <typename T>
        static JSValue create_js_value(JSContext* context, T&& v) {
            using Decayed = std::decay_t<T>;
            if constexpr (std::is_same_v<Decayed, bool>) {
                return JS_NewBool(context, v);
            } else if constexpr (std::is_integral_v<Decayed>) {
                return JS_NewInt32(context, static_cast<int32_t>(v));
            } else if constexpr (std::is_floating_point_v<Decayed>) {
                return JS_NewFloat64(context, static_cast<double>(v));
            } else if constexpr (std::is_convertible_v<Decayed, std::string_view>) {
                const std::string_view sv = v;
                return JS_NewStringLen(context, sv.data(), sv.size());
            } else if constexpr (std::is_same_v<Decayed, Value>) {
                return JS_DupValue(context, v.get());
            } else if constexpr (std::is_class_v<Decayed>) {
                JSRuntime* rt = JS_GetRuntime(context);
                JSClassID class_id = 0;

                // Thread-safe lookup of the Class ID
                {
                    std::shared_lock lock(detail::JSClassInfo<Decayed>::mutex);
                    if (detail::JSClassInfo<Decayed>::ids.contains(rt)) {
                        class_id = detail::JSClassInfo<Decayed>::ids.at(rt);
                    }
                }

                if (class_id != 0) {
                    JSValue obj = JS_NewObjectClass(context, class_id);
                    if (!JS_IsException(obj)) {
                        auto* ptr = new Decayed(std::forward<T>(v));
                        JS_SetOpaque(obj, ptr);
                        return obj;
                    }
                }
                return JS_UNDEFINED;
            } else if constexpr (std::is_integral_v<Decayed> || std::is_enum_v<Decayed>) {
                return JS_NewInt32(context, static_cast<int32_t>(v));
            } else {
                static_assert(std::is_void_v<T>, "Attempted to create JSValue from unsupported type.");
            }
            return JS_UNDEFINED;
        }

        // --- Fluent High-Level API ---

        template <typename T>
        Value& set_variable(const std::string_view name, T&& v) {
            JS_SetPropertyStr(ctx, val, name.data(), create_js_value(ctx, std::forward<T>(v)));
            return *this;
        }

        template <typename T>
        Value& set_constant(const std::string_view name, T&& v) {
            JS_DefinePropertyValueStr(ctx, val, name.data(), create_js_value(ctx, std::forward<T>(v)), JS_PROP_ENUMERABLE);
            return *this;
        }

        Value& set_cfunction(const std::string_view name, JSCFunction* func, const int length = 0);

        template <typename F>
        Value& set_function(const std::string_view name, F&& func) {
            using DecayedF = std::decay_t<F>;
            using Traits = detail::function_traits<DecayedF>;

            auto* func_ptr = new DecayedF(std::forward<F>(func));

            JSValue data_obj = JS_NewArrayBuffer(
                ctx,
                reinterpret_cast<uint8_t*>(func_ptr),
                sizeof(DecayedF),
                detail::lambda_free_func<DecayedF>,
                nullptr,
                false
            );

            if (JS_IsException(data_obj)) {
                delete func_ptr;
                throw std::runtime_error("qjs::Value error: Failed to allocate ArrayBuffer for closure.");
            }

            JSValue js_func = JS_NewCFunctionData(
                ctx,
                detail::cpp_closure_trampoline<DecayedF>,
                Traits::arity,
                0,
                1,
                &data_obj
            );

            JS_SetPropertyStr(ctx, val, name.data(), js_func);
            JS_FreeValue(ctx, data_obj);

            return *this;
        }

        template <typename T>
        Value& set_element(const uint32_t index, T&& v) {
            if (!is_array()) {
                const JSValue new_val = create_js_value(ctx, std::forward<T>(v));
                JS_FreeValue(ctx, new_val);
                throw std::runtime_error("qjs::Value error: set_element called on a non-array value.");
            }

            int result = JS_SetPropertyUint32(ctx, val, index, create_js_value(ctx, std::forward<T>(v)));
            if (result < 0) {
                throw std::runtime_error("qjs::Value error: QuickJS failed to set the array element.");
            }

            return *this;
        }

        // --- Retrieval & Checking ---

        [[nodiscard]] Value get_property(std::string_view name) const;
        [[nodiscard]] bool has_property(std::string_view name) const;
        [[nodiscard]] Value get_element(uint32_t index) const;

        template <typename T>
        [[nodiscard]] std::optional<T> as() const {
            using Decayed = std::decay_t<T>;

            if constexpr (std::is_same_v<Decayed, Value>) {
                return *this;
            } else if constexpr (std::is_same_v<Decayed, bool>) {
                if (int v = JS_ToBool(ctx, val); v != -1) return v == 1;
            } else if constexpr (std::is_integral_v<Decayed>) {
                int64_t v;
                if (JS_ToInt64(ctx, &v, val) == 0) return static_cast<T>(v);
            } else if constexpr (std::is_floating_point_v<Decayed>) {
                double v;
                if (JS_ToFloat64(ctx, &v, val) == 0) return static_cast<T>(v);
            } else if constexpr (std::is_same_v<Decayed, std::string>) {
                if (!is_string() && !is_number()) return std::nullopt;
                if (const char* cstr = JS_ToCString(ctx, val)) {
                    std::string str(cstr);
                    JS_FreeCString(ctx, cstr);
                    return str;
                } else {
                    JS_FreeValue(ctx, JS_GetException(ctx));
                }
            } else if constexpr (std::is_same_v<Decayed, std::string_view>) {
                if (!is_string() && !is_number()) return std::nullopt;
                size_t len;
                if (const char* cstr = JS_ToCStringLen(ctx, &len, val)) {
                    std::string_view sv(cstr, len);
                    return sv;
                } else {
                    JS_FreeValue(ctx, JS_GetException(ctx));
                }
            } else if constexpr (std::is_class_v<Decayed>) {
                JSRuntime* rt = JS_GetRuntime(ctx);
                JSClassID class_id = 0;

                // Thread-safe read for the Class ID
                {
                    std::shared_lock lock(detail::JSClassInfo<Decayed>::mutex);
                    if (detail::JSClassInfo<Decayed>::ids.contains(rt)) {
                        class_id = detail::JSClassInfo<Decayed>::ids.at(rt);
                    }
                }

                if (class_id != 0) {
                    void* opaque = JS_GetOpaque(val, class_id);
                    if (opaque) {
                        return *static_cast<Decayed*>(opaque);
                    }
                }
                return std::nullopt;
            } else {
                static_assert(std::is_void_v<T>, "Unsupported type requested in as<T>()");
            }
            return std::nullopt;
        }

        // --- Type Checking ---
        [[nodiscard]] bool is_undefined() const;
        [[nodiscard]] bool is_null() const;
        [[nodiscard]] bool is_bool() const;
        [[nodiscard]] bool is_number() const;
        [[nodiscard]] bool is_string() const;
        [[nodiscard]] bool is_object() const;
        [[nodiscard]] bool is_array() const;
        [[nodiscard]] bool is_function() const;
        [[nodiscard]] bool is_error() const;
        [[nodiscard]] bool is_exception() const;

        [[nodiscard]] Value call(const std::vector<Value>& args, const Value& this_obj) const;
        [[nodiscard]] Value call(const std::vector<Value>& args = {}) const;

        static Value create_object(JSContext* ctx);
        static Value create_array(JSContext* ctx);

        [[nodiscard]] JSValue get() const { return val; }
        [[nodiscard]] JSContext* context() const { return ctx; }

    private:
        JSContext* ctx{nullptr};
        JSValue val{JS_UNDEFINED};
    };

    namespace detail {

        template <typename F, typename Traits, std::size_t... I>
        JSValue cpp_closure_trampoline_impl(JSContext* ctx, F* func, int argc, JSValueConst* argv, std::index_sequence<I...>) {
            try {
                // Helper lambda to safely extract a single argument based on index I
                auto extract_arg = [&]<std::size_t Index>() {
                    using TargetType = std::tuple_element_t<Index, typename Traits::args_tuple>;

                    // Decayed extraction type: treat string_view/const char* as std::string to ensure copy and safe internal cleanup
                    using ExtractType = std::conditional_t<
                        std::is_same_v<TargetType, std::string_view> || std::is_same_v<TargetType, const char*>,
                        std::string,
                        TargetType
                    >;

                    if (Index < static_cast<std::size_t>(argc)) {
                        return Value(ctx, JS_DupValue(ctx, argv[Index]))
                            .template as<ExtractType>()
                            .value_or(TargetType{}); // TargetType{} is convertible to ExtractType
                    } else {
                        return TargetType{};
                    }
                };

                auto args_tuple = std::make_tuple(extract_arg.template operator()<I>()...);

                JSValue result_val;
                if constexpr (std::is_void_v<typename Traits::result_type>) {
                    std::apply(*func, args_tuple);
                    result_val = JS_UNDEFINED;
                } else {
                    auto result = std::apply(*func, args_tuple);
                    result_val = Value::create_js_value(ctx, result);
                }

                return result_val;
            } catch (const std::exception& e) {
                return JS_ThrowInternalError(ctx, "%s", e.what());
            }
        }

        template <typename F>
        JSValue cpp_closure_trampoline(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* func_data) {
            using Traits = function_traits<F>;

            size_t size = 0;
            uint8_t* ptr = JS_GetArrayBuffer(ctx, &size, func_data[0]);
            if (!ptr) {
                return JS_UNDEFINED;
            }

            F* func = reinterpret_cast<F*>(ptr);
            return cpp_closure_trampoline_impl<F, Traits>(ctx, func, argc, argv, std::make_index_sequence<Traits::arity>{});
        }
    }
}