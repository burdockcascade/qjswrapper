#pragma once

#include <string>
#include <type_traits>

#include "object.hpp"
#include "util.hpp"

namespace qjs {

    template<typename T>
    class Class {
    public:
        Class(Object constructor, Object prototype, ConstructorDispatcher<T>* dispatcher)
            : constructor_(std::move(constructor)), prototype_(std::move(prototype)), dispatcher_(dispatcher) {}

        template<typename... Args>
        Class& constructor() {
            auto factory = [](JSContext* ctx, int argc, JSValueConst* argv) -> T* {
                auto args = [&]<size_t... Is>(std::index_sequence<Is...>) {
                    return std::make_tuple(converter<std::decay_t<Args>>::get(ctx, (Is < argc ? argv[Is] : JS_UNDEFINED))...);
                }(std::index_sequence_for<Args...>{});

                return std::apply([](auto&&... unpacked) {
                    return new T(std::forward<decltype(unpacked)>(unpacked)...);
                }, std::move(args));
            };

            dispatcher_->overloads.push_back({sizeof...(Args), factory});
            return *this;
        }

        template<typename F>
        requires callable<F>
        Class& constructor(F&& func) {
            using traits = function_traits<std::decay_t<F>>;
            register_custom_factory(std::forward<F>(func), typename traits::args_tuple{});
            return *this;
        }

        template<typename F>
        requires (callable<F> || std::is_member_function_pointer_v<std::decay_t<F>>)
        Class& method(std::string_view name, F&& func) {
            prototype_.set_function(name, std::forward<F>(func));
            return *this;
        }

        template<typename F>
        requires callable<F>
        Class& static_method(std::string_view name, F&& func) {
            constructor_.set_function(name, std::forward<F>(func));
            return *this;
        }

        template<typename V>
        requires (!callable<V> && !std::is_member_function_pointer_v<std::decay_t<V>>)
        Class& variable(std::string_view name, V&& value) {
            prototype_.set_variable(name, std::forward<V>(value));
            return *this;
        }

        template<typename V>
        requires (!callable<V> && !std::is_member_function_pointer_v<std::decay_t<V>>)
        Class& constant(std::string_view name, V&& value) {
            prototype_.set_constant(name, std::forward<V>(value));
            return *this;
        }

        template<typename V>
        requires (!callable<V> && !std::is_member_function_pointer_v<std::decay_t<V>>)
        Class& static_variable(std::string_view name, V&& value) {
            constructor_.set_variable(name, std::forward<V>(value));
            return *this;
        }

        template<typename V>
        requires (!callable<V> && !std::is_member_function_pointer_v<std::decay_t<V>>)
        Class& static_constant(std::string_view name, V&& value) {
            constructor_.set_constant(name, std::forward<V>(value));
            return *this;
        }

        [[nodiscard]] const Object& get_constructor() const {
            return constructor_;
        }

    private:
        Object constructor_;
        Object prototype_;
        ConstructorDispatcher<T>* dispatcher_;

        template<typename F, typename... Args>
        void register_custom_factory(F func, std::tuple<Args...>) {
            using traits = function_traits<std::decay_t<F>>;
            using Ret = traits::return_type;
            constexpr size_t arity = traits::arity;

            auto factory = [f = std::move(func)](JSContext* ctx, int argc, JSValueConst* argv) -> T* {
                auto args = [&]<size_t... Is>(std::index_sequence<Is...>) {
                    return std::make_tuple(converter<std::decay_t<Args>>::get(ctx, (Is < argc ? argv[Is] : JS_UNDEFINED))...);
                }(std::index_sequence_for<Args...>{});

                if constexpr (std::is_pointer_v<Ret> && std::is_same_v<std::remove_pointer_t<Ret>, T>) {
                    return std::apply(f, std::move(args));
                } else if constexpr (std::is_same_v<Ret, T>) {
                    return new T(std::apply(f, std::move(args)));
                } else {
                    static_assert(std::is_same_v<Ret, T> || (std::is_pointer_v<Ret> && std::is_same_v<std::remove_pointer_t<Ret>, T>),
                                  "Custom constructor must return T or T*");
                    return nullptr;
                }
            };

            dispatcher_->overloads.push_back({arity, factory});
        }
    };

} // namespace qjs