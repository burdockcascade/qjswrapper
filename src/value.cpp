#include "value.hpp"

namespace qjswrapper {

    Value::Value(JSContext* ctx, const JSValue val) : ctx(ctx), val(val) {}

    Value::~Value() {
        if (ctx) {
            JS_FreeValue(ctx, val);
        }
    }

    Value::Value(const Value& other) : ctx(other.ctx) {
        val = JS_DupValue(ctx, other.val);
    }

    Value& Value::operator=(const Value& other) {
        if (this != &other) {
            if (ctx) JS_FreeValue(ctx, val);
            ctx = other.ctx;
            val = JS_DupValue(ctx, other.val);
        }
        return *this;
    }

    Value::Value(Value&& other) noexcept
        : ctx(std::exchange(other.ctx, nullptr)),
          val(std::exchange(other.val, JS_UNDEFINED)) {}

    Value& Value::operator=(Value&& other) noexcept {
        if (this != &other) {
            if (ctx) JS_FreeValue(ctx, val);
            ctx = std::exchange(other.ctx, nullptr);
            val = std::exchange(other.val, JS_UNDEFINED);
        }
        return *this;
    }

    Value& Value::set_cfunction(const std::string_view name, JSCFunction* func, const int length) {
        JSValue js_func = JS_NewCFunction(ctx, func, name.data(), length);
        JS_SetPropertyStr(ctx, val, name.data(), js_func);
        return *this;
    }

    Value Value::get_property(const std::string_view name) const {
        return {ctx, JS_GetPropertyStr(ctx, val, name.data())};
    }

    bool Value::has_property(const std::string_view name) const {
        const JSAtom atom = JS_NewAtomLen(ctx, name.data(), name.size());
        if (atom == JS_ATOM_NULL) return false;
        const int has_prop = JS_HasProperty(ctx, val, atom);
        JS_FreeAtom(ctx, atom);
        return has_prop == 1;
    }

    Value Value::get_element(const uint32_t index) const {
        return {ctx, JS_GetPropertyUint32(ctx, val, index)};
    }

    bool Value::is_undefined() const { return JS_IsUndefined(val); }
    bool Value::is_null() const      { return JS_IsNull(val); }
    bool Value::is_bool() const      { return JS_IsBool(val); }
    bool Value::is_number() const    { return JS_IsNumber(val); }
    bool Value::is_string() const    { return JS_IsString(val); }
    bool Value::is_object() const    { return JS_IsObject(val); }
    bool Value::is_array() const     { return JS_IsArray(val) == 1; }
    bool Value::is_function() const  { return JS_IsFunction(ctx, val); }
    bool Value::is_error() const     { return JS_IsError(val); }
    bool Value::is_exception() const { return JS_IsException(val); }

    Value Value::call(const std::vector<Value>& args, const Value& this_obj) const {
        std::vector<JSValue> raw_args;
        raw_args.reserve(args.size());
        for (const auto& arg : args) {
            raw_args.push_back(arg.get());
        }

        const JSValue result = JS_Call(ctx, val, this_obj.get(), static_cast<int>(raw_args.size()), raw_args.data());
        return {ctx, result};
    }

    Value Value::call(const std::vector<Value>& args) const {
        return call(args, Value{});
    }

    Value Value::create_object(JSContext* ctx) {
        return {ctx, JS_NewObject(ctx)};
    }

    Value Value::create_array(JSContext* ctx) {
        return {ctx, JS_NewArray(ctx)};
    }
}