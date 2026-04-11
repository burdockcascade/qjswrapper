#include <catch2/catch_test_macros.hpp>
#include "../src/qjswrapper.hpp"

// A custom type to test user-extensibility, similar to the Color example
struct Vec2 {
    float x, y;
};

// Custom converter specialization for Vec2
template<>
struct qjs::converter<Vec2> {
    static Vec2 get(JSContext* ctx, JSValueConst v) {
        double x, y;
        JSValue x_val = JS_GetPropertyStr(ctx, v, "x");
        JSValue y_val = JS_GetPropertyStr(ctx, v, "y");

        JS_ToFloat64(ctx, &x, x_val);
        JS_ToFloat64(ctx, &y, y_val);

        JS_FreeValue(ctx, x_val);
        JS_FreeValue(ctx, y_val);

        return {static_cast<float>(x), static_cast<float>(y)};
    }

    static qjs::Value put(JSContext* ctx, const Vec2& v) {
        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, v.x));
        JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, v.y));
        return qjs::Value(ctx, obj);
    }
};

TEST_CASE("Converter System Verification", "[converters]") {
    qjs::Engine engine;
    JSContext* ctx = engine.global().as_value().ctx();

    SECTION("Primitive Data Types") {
        // Test Integer
        auto val_int = qjs::converter<int>::put(ctx, 42);
        CHECK(qjs::converter<int>::get(ctx, val_int.get()) == 42);

        // Test Double
        auto val_dbl = qjs::converter<double>::put(ctx, 3.14159);
        CHECK(qjs::converter<double>::get(ctx, val_dbl.get()) == 3.14159);

        // Test Boolean
        auto val_bool = qjs::converter<bool>::put(ctx, true);
        CHECK(qjs::converter<bool>::get(ctx, val_bool.get()) == true);

        // Test String
        std::string original = "QuickJS-NG Wrapper";
        auto val_str = qjs::converter<std::string>::put(ctx, original);
        CHECK(qjs::converter<std::string>::get(ctx, val_str.get()) == original);
    }

    SECTION("String Specializations") {
        // Test const char* (C-strings)
        const char* c_str = "Legacy String";
        auto val_c = qjs::converter<const char*>::put(ctx, c_str);
        std::string result = qjs::converter<const char*>::get(ctx, val_c.get());
        CHECK(result == "Legacy String");
    }

    SECTION("Object Wrapping") {
        // Testing the converter<Object> specialization
        auto obj = engine.make_object().set("id", 101);

        // Pass Object through the converter
        auto val_obj = qjs::converter<qjs::Object>::put(ctx, obj);
        qjs::Object extracted = qjs::converter<qjs::Object>::get(ctx, val_obj.get());

        CHECK(extracted.get<int>("id") == 101);
        CHECK(extracted.as_value().is_object() == true);
    }

    SECTION("Custom Type Extension (Vec2)") {
        Vec2 pos = {10.5f, -20.0f};

        // Convert C++ Struct -> JS Value
        auto val_vec = qjs::converter<Vec2>::put(ctx, pos);

        // Verify in JS context
        engine.global().set("pos", qjs::converter<Vec2>::get(ctx, val_vec.get()));
        auto js_check = engine.eval("pos.x === 10.5 && pos.y === -20", "test.js");
        CHECK(js_check.value() == "true");

        // Convert JS Value -> C++ Struct
        Vec2 round_trip = qjs::converter<Vec2>::get(ctx, val_vec.get());
        CHECK(round_trip.x == 10.5f);
        CHECK(round_trip.y == -20.0f);
    }
}