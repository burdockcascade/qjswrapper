#pragma once

#include "converter.hpp"
#include "util.hpp"

namespace qjs {

    enum class Prop {
        Normal,           // Writable | Enumerable | Configurable
        ReadOnly,         // Enumerable | Configurable
        Hidden,           // Writable | Configurable
        HiddenReadOnly,   // Configurable
        Locked,           // Enumerable (Not writable, not configurable)
        Internal          // None (Fully locked down and hidden)
    };

    class Object {
    public:
        explicit Object(Value v) : val_(std::move(v)) {}

        template<typename T>
        requires (!callable<T> && !std::is_member_function_pointer_v<std::decay_t<T>>)
        Object& set(std::string_view prop, T&& value, const Prop mode) {
            auto ctx = val_.ctx();
            const Value v = converter<std::decay_t<T>>::put(ctx, std::forward<T>(value));
            const int flags = resolve_flags(mode);
            JS_DefinePropertyValueStr(ctx, val_.get(), prop.data(), JS_DupValue(ctx, v.get()), flags);
            return *this;
        }

        template<typename T>
        requires (!callable<T> && !std::is_member_function_pointer_v<std::decay_t<T>>)
        Object& set_variable(std::string_view prop, T&& value) {
            return set(prop, std::forward<T>(value), Prop::Normal);
        }

        template<typename T>
        requires (!callable<T> && !std::is_member_function_pointer_v<std::decay_t<T>>)
        Object& set_constant(std::string_view prop, T&& value) {
            return set(prop, std::forward<T>(value), Prop::Locked);
        }

        template<typename F>
        requires (callable<F> || std::is_member_function_pointer_v<std::decay_t<F>>)
        Object& set(const std::string_view prop, F&& func, const Prop mode) {
            const auto ctx = val_.ctx();
            const JSValue js_func = create_js_function(ctx, std::forward<F>(func));
            const int flags = resolve_flags(mode);
            JS_DefinePropertyValueStr(ctx, val_.get(), prop.data(), js_func, flags);
            return *this;
        }

        template<typename F>
        requires (callable<F> || std::is_member_function_pointer_v<std::decay_t<F>>)
        Object& set_function(const std::string_view prop, F&& func) {
            return set(prop, std::forward<F>(func), Prop::ReadOnly);
        }

        template<typename T>
        T get(const std::string_view prop) const {
            auto ctx = val_.ctx();
            JSValue p = JS_GetPropertyStr(ctx, val_.get(), prop.data());
            T result = converter<T>::get(ctx, p);
            JS_FreeValue(ctx, p);
            return result;
        }

        [[nodiscard]] bool remove(const std::string_view prop) const {
            const auto ctx = val_.ctx();
            const JSAtom atom = JS_NewAtomLen(ctx, prop.data(), prop.size());
            const int ret = JS_DeleteProperty(ctx, val_.get(), atom, 0);
            JS_FreeAtom(ctx, atom);
            return ret == 1;
        }

        template<typename... Args>
        Value invoke(const std::string_view prop, Args&&... args) const {
            auto ctx = val_.ctx();
            const JSValue func = JS_GetPropertyStr(ctx, val_.get(), prop.data());

            if (!JS_IsFunction(ctx, func)) {
                JS_FreeValue(ctx, func);
                return { ctx, JS_ThrowTypeError(ctx, "Property '%s' is not a function", prop.data()) };
            }

            std::vector<Value> managed_args;
            managed_args.reserve(sizeof...(Args));
            (managed_args.push_back(converter<std::decay_t<Args>>::put(ctx, std::forward<Args>(args))), ...);

            std::vector<JSValue> raw_args;
            raw_args.reserve(sizeof...(Args));
            for (const auto& arg : managed_args) raw_args.push_back(arg.get());

            JSValue result = JS_Call(ctx, func, val_.get(), raw_args.size(), raw_args.data());
            JS_FreeValue(ctx, func);

            return { ctx, result };
        }

        [[nodiscard]] std::vector<std::string> keys() const {
            const auto ctx = val_.ctx();
            JSPropertyEnum *ptab = nullptr;
            uint32_t plen = 0;
            std::vector<std::string> result;

            if (JS_GetOwnPropertyNames(ctx, &ptab, &plen, val_.get(), JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
                result.reserve(plen);
                for(uint32_t i = 0; i < plen; i++) {
                    if (const char* str = JS_AtomToCString(ctx, ptab[i].atom)) {
                        result.emplace_back(str);
                        JS_FreeCString(ctx, str);
                    }
                    JS_FreeAtom(ctx, ptab[i].atom);
                }
                js_free(ctx, ptab);
            }
            return result;
        }

        [[nodiscard]] bool has(const std::string_view prop) const {
            const JSAtom atom = JS_NewAtomLen(val_.ctx(), prop.data(), prop.size());
            const int ret = JS_HasProperty(val_.ctx(), val_.get(), atom);
            JS_FreeAtom(val_.ctx(), atom);
            return ret > 0;
        }

        [[nodiscard]] const Value& as_value() const { return val_; }

    private:
        Value val_;

        static int resolve_flags(const Prop mode) {
            switch (mode) {
                case Prop::Normal:         return JS_PROP_WRITABLE | JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE;
                case Prop::ReadOnly:       return JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE;
                case Prop::Hidden:         return JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE;
                case Prop::HiddenReadOnly: return JS_PROP_CONFIGURABLE;
                case Prop::Locked:         return JS_PROP_ENUMERABLE;
                case Prop::Internal:       return 0; // No flags
                default:                   return JS_PROP_WRITABLE | JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE;
            }
        }
    };

    template<>
    struct converter<Object> {
        static Value put(JSContext* ctx, const Object& val) {
            return val.as_value();
        }

        static Object get(JSContext* ctx, JSValueConst v) {
            return Object(Value(ctx, JS_DupValue(ctx, v)));
        }
    };

} // namespace qjs