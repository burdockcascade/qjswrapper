#pragma once

#include <string>
#include <functional>
#include <type_traits>
#include <quickjs.h>

#include "object.hpp"
#include "class.hpp"
#include "converter.hpp"
#include "util.hpp"

namespace qjs {

    class Module {
    public:
        Module(JSContext* ctx, JSModuleDef* m, std::function<void(std::string, Value)> add_cb)
            : ctx_(ctx), m_(m), add_cb_(std::move(add_cb)) {}

        template<typename T>
        requires (!callable<T> && !std::is_member_function_pointer_v<std::decay_t<T>>)
        Module& add(const std::string& name, T&& value) {
            JS_AddModuleExport(ctx_, m_, name.c_str());
            add_cb_(name, converter<std::decay_t<T>>::put(ctx_, std::forward<T>(value)));
            return *this;
        }

        template<typename F>
        requires (callable<F> || std::is_member_function_pointer_v<std::decay_t<F>>)
        Module& add(const std::string& name, F&& func) {
            JS_AddModuleExport(ctx_, m_, name.c_str());
            const JSValue js_func = create_js_function(ctx_, std::forward<F>(func));
            add_cb_(name, Value(ctx_, js_func));
            return *this;
        }

        template<typename T>
        Module& add(const std::string& name, Class<T>& cls) {
            return add(name, cls.get_constructor());
        }

        template<typename T>
        Module& add(const std::string& name, const Class<T>& cls) {
            return add(name, cls.get_constructor());
        }

    private:
        JSContext* ctx_;
        JSModuleDef* m_;
        std::function<void(std::string, Value)> add_cb_;
    };

} // namespace qjs