#pragma once

#include <type_traits>
#include <tuple>
#include <functional>

#include <quickjs.h>
#include "converter.hpp"

namespace qjs {

    // --- Function Traits for Type Deduction ---
    template<typename T>
    struct function_traits : function_traits<decltype(&T::operator())> {};

    template<typename R, typename... Args>
    struct function_traits<R(*)(Args...)> {
        using return_type = R;
        using args_tuple = std::tuple<Args...>;
        static constexpr size_t arity = sizeof...(Args);
    };

    template<typename C, typename R, typename... Args>
    struct function_traits<R(C::*)(Args...) const> {
        using return_type = R;
        using args_tuple = std::tuple<Args...>;
        static constexpr size_t arity = sizeof...(Args);
    };

    template<typename C, typename R, typename... Args>
    struct function_traits<R(C::*)(Args...)> {
        using return_type = R;
        using args_tuple = std::tuple<Args...>;
        static constexpr size_t arity = sizeof...(Args);
    };

    // --- Concepts ---
    template<typename T>
    concept callable = (std::is_class_v<std::remove_cvref_t<T>> && requires {
        &std::remove_cvref_t<T>::operator();
    }) || std::is_function_v<std::remove_pointer_t<std::remove_cvref_t<T>>>;

    struct CallableBase {
        virtual ~CallableBase() = default;
    };

    // Strongly-typed wrapper holding the exact functor (avoids std::function overhead)
    template<typename F>
    struct CallableWrapper : CallableBase {
        F func;
        explicit CallableWrapper(F f) : func(std::move(f)) {}
    };

    // --- Bridge Components ---
    inline JSClassID wrapper_class_id = 0;

    template<typename F, typename R, typename ArgsTuple>
    struct Invoker;

    template<typename F, typename R, typename... Args>
    struct Invoker<F, R, std::tuple<Args...>> {
        static JSValue apply(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv, int /*magic*/, JSValue* data) {
            void* p = JS_GetOpaque(data[0], wrapper_class_id);
            if (!p) {
                return JS_ThrowTypeError(ctx, "Failed to retrieve C++ lambda closure");
            }

            auto* wrapper = static_cast<CallableWrapper<F>*>(p);
            auto& func = wrapper->func;

            auto args = [&]<size_t... Is>(std::index_sequence<Is...>) {
                return std::make_tuple(converter<std::decay_t<Args>>::get(ctx, (Is < argc ? argv[Is] : JS_UNDEFINED))...);
            }(std::index_sequence_for<Args...>{});

            try {
                if constexpr (std::is_void_v<R>) {
                    std::apply(func, std::move(args));
                    return JS_UNDEFINED;
                } else {
                    Value ret = converter<R>::put(ctx, std::apply(func, std::move(args)));
                    return JS_DupValue(ctx, ret.get());
                }
            } catch (const std::exception& e) {
                return JS_ThrowInternalError(ctx, "%s", e.what());
            } catch (...) {
                return JS_ThrowInternalError(ctx, "Unknown C++ exception occurred");
            }
        }
    };

    // A helper to track Class IDs for C++ types
    template<typename T>
    struct ClassRegistry {
        static inline JSClassID class_id = 0;
        static inline std::string name;

        static void init(JSRuntime* rt, const std::string& class_name) {
            if (class_id == 0) {
                JS_NewClassID(rt, &class_id);
                name = class_name;
            }

            if (!JS_IsRegisteredClass(rt, class_id)) {
                JSClassDef def{
                    .class_name = name.c_str(),
                    .finalizer = [](JSRuntime* /*rt*/, JSValue val) {
                        auto* obj = static_cast<T*>(JS_GetOpaque(val, class_id));
                        delete obj; // JS GC calls C++ destructor
                    }
                };
                JS_NewClass(rt, class_id, &def);
            }
        }
    };

    template<typename MemFunc>
    struct MemberCallableWrapper : CallableBase {
        MemFunc func;
        explicit MemberCallableWrapper(MemFunc f) : func(f) {}
    };

    inline void init_wrapper_class(JSContext* ctx) {
        auto rt = JS_GetRuntime(ctx);
        if (wrapper_class_id == 0) {
            JS_NewClassID(rt, &wrapper_class_id);
        }
        if (!JS_IsRegisteredClass(rt, wrapper_class_id)) {
            JSClassDef def{
                .class_name = "CppLambda",
                .finalizer = [](JSRuntime* /*rt*/, JSValue val) {
                    auto* base = static_cast<CallableBase*>(JS_GetOpaque(val, wrapper_class_id));
                    delete base;
                }
            };
            JS_NewClass(rt, wrapper_class_id, &def);
        }
    }

    template<typename MethodType, typename C, typename R, typename... Args>
    struct MemberInvokerImpl {
        static JSValue apply(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int /*magic*/, JSValue* data) {
            C* instance = static_cast<C*>(JS_GetOpaque(this_val, ClassRegistry<C>::class_id));
            if (!instance) return JS_ThrowTypeError(ctx, "Method called on incompatible object");

            void* p = JS_GetOpaque(data[0], wrapper_class_id);
            if (!p) return JS_ThrowTypeError(ctx, "Failed to retrieve member function pointer");

            auto* wrapper = static_cast<MemberCallableWrapper<MethodType>*>(p);
            MethodType method_ptr = wrapper->func;

            auto args = [&]<size_t... Is>(std::index_sequence<Is...>) {
                return std::make_tuple(converter<std::decay_t<Args>>::get(ctx, (Is < argc ? argv[Is] : JS_UNDEFINED))...);
            }(std::index_sequence_for<Args...>{});

            try {
                auto caller = [instance, method_ptr](auto&&... unpacked) {
                    return (instance->*method_ptr)(std::forward<decltype(unpacked)>(unpacked)...);
                };

                if constexpr (std::is_void_v<R>) {
                    std::apply(caller, std::move(args));
                    return JS_UNDEFINED;
                } else {
                    Value ret = converter<R>::put(ctx, std::apply(caller, std::move(args)));
                    return JS_DupValue(ctx, ret.get());
                }
            } catch (const std::exception& e) {
                return JS_ThrowInternalError(ctx, "%s", e.what());
            } catch (...) {
                return JS_ThrowInternalError(ctx, "Unknown exception in member function");
            }
        }
    };

    template<typename F>
    requires callable<F>
    JSValue create_js_function(JSContext* ctx, F&& func) {
        using traits = function_traits<std::decay_t<F>>;
        using R = typename traits::return_type;
        using ArgsTuple = typename traits::args_tuple;
        using DecayF = std::decay_t<F>; // Deduce the raw functor

        init_wrapper_class(ctx);

        JSValue opaque_obj = JS_NewObjectClass(ctx, wrapper_class_id);
        auto* func_ptr = new CallableWrapper<DecayF>(std::forward<F>(func));
        JS_SetOpaque(opaque_obj, func_ptr);

        JSValue js_func = JS_NewCFunctionData(ctx,
            &Invoker<DecayF, R, ArgsTuple>::apply,
            static_cast<int>(traits::arity), 0, 1, &opaque_obj
        );

        JS_FreeValue(ctx, opaque_obj);
        return js_func;
    }

    template<typename C, typename R, typename... Args>
    JSValue create_js_function(JSContext* ctx, R (C::*method)(Args...)) {
        init_wrapper_class(ctx);
        using MemFunc = R (C::*)(Args...);

        JSValue opaque_obj = JS_NewObjectClass(ctx, wrapper_class_id);
        auto* func_ptr = new MemberCallableWrapper<MemFunc>(method);
        JS_SetOpaque(opaque_obj, func_ptr);

        JSValue js_func = JS_NewCFunctionData(ctx,
            &MemberInvokerImpl<MemFunc, C, R, Args...>::apply,
            sizeof...(Args), 0, 1, &opaque_obj
        );

        JS_FreeValue(ctx, opaque_obj);
        return js_func;
    }

    template<typename C, typename R, typename... Args>
    JSValue create_js_function(JSContext* ctx, R (C::*method)(Args...) const) {
        init_wrapper_class(ctx);
        using MemFunc = R (C::*)(Args...) const;

        JSValue opaque_obj = JS_NewObjectClass(ctx, wrapper_class_id);
        auto* func_ptr = new MemberCallableWrapper<MemFunc>(method);
        JS_SetOpaque(opaque_obj, func_ptr);

        JSValue js_func = JS_NewCFunctionData(ctx,
            &MemberInvokerImpl<MemFunc, C, R, Args...>::apply,
            sizeof...(Args), 0, 1, &opaque_obj
        );

        JS_FreeValue(ctx, opaque_obj);
        return js_func;
    }

    // --- Constructor Dispatcher ---
    // Manages multiple constructor overloads for a specific class T
    template<typename T>
    struct ConstructorDispatcher : CallableBase {
        using Factory = std::function<T*(JSContext*, int, JSValueConst*)>;

        struct Overload {
            size_t arity;
            Factory factory;
        };
        std::vector<Overload> overloads;

        static JSValue apply(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv, int /*magic*/, JSValue* data) {
            void* p = JS_GetOpaque(data[0], wrapper_class_id);
            if (!p) return JS_ThrowTypeError(ctx, "Failed to retrieve constructor dispatcher");

            auto* dispatcher = static_cast<ConstructorDispatcher<T>*>(p);

            for (const auto& overload : dispatcher->overloads) {
                if (static_cast<size_t>(argc) == overload.arity) {
                    try {
                        T* instance = overload.factory(ctx, argc, argv);
                        if (instance) {
                            JSValue obj = JS_NewObjectClass(ctx, ClassRegistry<T>::class_id);
                            if (JS_IsException(obj)) {
                                delete instance;
                                return obj;
                            }
                            JS_SetOpaque(obj, instance);
                            return obj;
                        }
                    } catch (const std::exception& e) {
                        return JS_ThrowInternalError(ctx, "Constructor error: %s", e.what());
                    } catch (...) {
                        return JS_ThrowInternalError(ctx, "Unknown exception in constructor");
                    }
                }
            }
            return JS_ThrowTypeError(ctx, "No matching constructor found for given argument count");
        }
    };

} // namespace qjs