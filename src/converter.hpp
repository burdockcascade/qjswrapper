#pragma once

#include <iostream>
#include <string>
#include <concepts>
#include <type_traits>

#include <quickjs.h>

namespace qjs {

    class Value {
    public:
        // Constructors
        Value(JSContext* ctx, JSValue v) : ctx_(ctx), v_(v) {}

        // Disable default constructor to ensure context is always present
        Value() = delete;

        // Destructor handles memory cleanup
        ~Value() {
            if (ctx_) {
                JS_FreeValue(ctx_, v_);
            }
        }

        // Copy semantics: Increment reference count
        Value(const Value& other) : ctx_(other.ctx_), v_(JS_DupValue(other.ctx_, other.v_)) {}

        Value& operator=(const Value& other) {
            if (this != &other) {
                if (ctx_) JS_FreeValue(ctx_, v_);
                ctx_ = other.ctx_;
                v_ = JS_DupValue(other.ctx_, other.v_);
            }
            return *this;
        }

        // Move semantics: Transfer ownership without changing ref count
        Value(Value&& other) noexcept : ctx_(other.ctx_), v_(other.v_) {
            other.ctx_ = nullptr;
            other.v_ = JS_UNDEFINED;
        }

        Value& operator=(Value&& other) noexcept {
            if (this != &other) {
                if (ctx_) JS_FreeValue(ctx_, v_);
                ctx_ = other.ctx_;
                v_ = other.v_;
                other.ctx_ = nullptr;
                other.v_ = JS_UNDEFINED;
            }
            return *this;
        }

        [[nodiscard]] bool is_object() const { return JS_IsObject(v_); }
        [[nodiscard]] bool is_exception() const { return JS_IsException(v_); }

        // Accessors
        [[nodiscard]] JSValue get() const { return v_; }
        [[nodiscard]] JSContext* ctx() const { return ctx_; }

        // Implicit conversion for compatibility with QuickJS C functions
        operator JSValue() const { return v_; }

    private:
        JSContext* ctx_;
        JSValue v_;
    };

    template<typename T>
    struct converter {
        static T get(JSContext* ctx, JSValueConst v) {
            if constexpr (std::is_same_v<T, bool>) {
                // Explicitly check for truthiness
                return JS_ToBool(ctx, v) > 0;
            } else if constexpr (std::floating_point<T>) {
                // Check floating point before general integrals
                double val = 0;
                JS_ToFloat64(ctx, &val, v);
                return static_cast<T>(val);
            } else if constexpr (std::integral<T>) {
                // Use 64-bit to prevent truncation during extraction
                int64_t val = 0;
                JS_ToInt64(ctx, &val, v);
                return static_cast<T>(val);
            } else if constexpr (std::is_convertible_v<T, std::string>) {
                size_t len;
                const char* str = JS_ToCStringLen(ctx, &len, v);
                std::string s(str ? str : "", len);
                JS_FreeCString(ctx, str);
                return s;
            }
            return T{};
        }

        static Value put(JSContext* ctx, const T& val) {
            if constexpr (std::is_same_v<T, bool>) {
                return { ctx, JS_NewBool(ctx, val) };
            } else if constexpr (std::floating_point<T>) {
                return { ctx, JS_NewFloat64(ctx, static_cast<double>(val)) };
            } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
                return { ctx, JS_NewInt64(ctx, static_cast<int64_t>(val)) };
            } else if constexpr (std::is_convertible_v<T, std::string_view>) {
                return { ctx, JS_NewStringLen(ctx, val.data(), val.size()) };
            }
            return { ctx, JS_UNDEFINED };
        }
    };

    template<>
    struct converter<const char*> {
        static const char* get(JSContext* ctx, JSValueConst v) {
            return JS_ToCString(ctx, v);
        }

        static Value put(JSContext* ctx, const char* val) {
            return { ctx, JS_NewString(ctx, val) };
        }
    };

} // namespace qjs