#pragma once

#include "eryscript/eryscript.hpp"
#include <array>
#include <cmath>
#include <concepts>
#include <limits>
#include <meta>
#include <optional>
#include <tuple>
#include <typeindex>
#include <unordered_set>

namespace eryscript {
namespace reflection_detail {
constexpr auto access = std::meta::access_context::unprivileged();

struct Candidate {
    std::function<int(const Arguments&)> match;
    Function call;
    std::size_t arity = std::numeric_limits<std::size_t>::max();
    std::string generic;
};
using Overloads = std::unordered_map<std::string, std::vector<Candidate>>;
inline std::string typeName(std::string name) {
    for (char& c : name) if (c == ':') c = '.';
    for (auto i = name.find(".."); i != std::string::npos; i = name.find("..")) name.erase(i, 1);
    return name;
}
inline Value dispatch(const std::vector<Candidate>& candidates, Arguments& args) {
    const Candidate* best = nullptr;
    int score = std::numeric_limits<int>::max();
    bool ambiguous = false;
    for (const auto& candidate : candidates) {
        const int current = candidate.match(args);
        if (current >= 0 && current < score) { best = &candidate; score = current; ambiguous = false; }
        else if (current >= 0 && current == score) ambiguous = true;
    }
    if (!best) throw std::runtime_error("No matching C++ overload");
    if (ambiguous) throw std::runtime_error("Ambiguous C++ overload");
    return best->call(args);
}

inline Function overloadFunction(std::vector<Candidate> methods) {
    return [methods = std::move(methods), arity = std::numeric_limits<std::size_t>::max(),
        types = std::vector<std::string>{}, selected = std::shared_ptr<const std::vector<Candidate>>{}]
        (Arguments& args) mutable {
        if (!selected || arity != args.values.size() || types != args.typeNames) {
            auto filtered = std::make_shared<std::vector<Candidate>>();
            const auto type = args.typeNames.size() == 1 ? typeName(args.typeNames[0]) : std::string{};
            for (const auto& method : methods) {
                if (method.arity == std::numeric_limits<std::size_t>::max()) {
                    filtered->push_back(method); // Candidates without signature metadata keep the general path.
                } else if (method.arity == args.values.size()) {
                    const auto expected = type.find('.') == std::string::npos ?
                        method.generic.substr(method.generic.find_last_of('.') + 1) : method.generic;
                    if (method.generic.empty() ? args.typeNames.empty() : args.typeNames.size() == 1 && type == expected)
                        filtered->push_back(method);
                }
            }
            arity = args.values.size(); types = args.typeNames; selected = std::move(filtered);
        }
        // Hold a snapshot across callbacks that may reenter with another signature.
        const auto candidates = selected;
        // Numeric values, ref/out modes, constness and lifetimes are still checked on every call.
        return dispatch(*candidates, args);
    };
}

template<class T> struct Shared : std::false_type {};
template<class T> struct Shared<std::shared_ptr<T>> : std::true_type { using Element = T; };
template<class T> struct Unique : std::false_type {};
template<class T, class D> struct Unique<std::unique_ptr<T, D>> : std::true_type { using Element = T; };
template<class T> struct Callback : std::false_type {};
template<class R, class... A> struct Callback<std::function<R(A...)>> : std::true_type {
    using Parameters = std::tuple<A...>;
};
template<class T> concept Sequence = requires(T& value) {
    typename T::value_type;
    value.begin(); value.end(); value.size();
} && !std::same_as<T, std::string> && !requires { typename T::key_type; };

struct Context;
using Native = Object::Native;
template<class T> Value pack(T&& value, const std::shared_ptr<Context>& context,
    std::shared_ptr<Native> parent = {});
template<class T> T convert(const Value& value, const std::shared_ptr<Context>& context);
template<class F> Value packCallback(F function, const std::shared_ptr<Context>& context, std::shared_ptr<Native> parent);
template<class T> T* pointer(const Value& value) {
    if (value.isNull()) return nullptr;
    const auto native = value.object()->native;
    if (!native) throw std::runtime_error("Expected a reflected C++ object");
    native->check();
    if constexpr (!std::is_const_v<T>) {
        if (native->readOnly) throw std::runtime_error("C++ object is read-only");
    }
    const void* address = native->address(typeid(std::remove_cv_t<T>));
    if (!address) throw std::runtime_error("Incorrect C++ object type");
    return static_cast<T*>(const_cast<void*>(address));
}

template<class T> const void* cast(const T* value, const std::type_info& type) {
    using U = std::remove_cv_t<T>;
    if (type == typeid(U)) return value;
    if constexpr (std::is_class_v<U>) {
        template for (constexpr auto base : std::define_static_array(std::meta::bases_of(^^U, access))) {
            if constexpr (std::meta::is_public(base)) {
                using B = [:std::meta::type_of(base):];
                if (const auto result = cast(static_cast<const B*>(value), type)) return result;
            }
        }
    }
    return nullptr;
}

template<class T> struct Box : Native {
    std::function<T*()> get;
    std::weak_ptr<Context> context;
    std::shared_ptr<Native> parent;
    const void* address(const std::type_info& type) const override { return cast(get(), type); }
    const std::type_info& type() const override { return typeid(std::remove_cv_t<T>); }
    void check() const override;
    bool equals(const Native& other) const override {
        check(); other.check();
        const auto* value = static_cast<const std::remove_cv_t<T>*>(other.address(typeid(std::remove_cv_t<T>)));
        if constexpr (std::is_enum_v<T>) return value && *get() == *value;
        else if (value) return get() == value;
        else {
            const void* base = address(other.type());
            return base && base == other.address(other.type());
        }
    }
};

struct Descriptor {
    std::function<Value(const std::shared_ptr<Native>&, const std::shared_ptr<Context>&)> view;
};
struct Context {
    bool alive = true;
    std::function<void(const Native&)> validate;
    std::function<void(const std::string&)> onError;
    std::unordered_map<std::type_index, Descriptor> types;
};
struct KeepAlive : Native {
    std::vector<std::shared_ptr<Native>> objects;
    std::vector<std::shared_ptr<void>> storage;
    const void* address(const std::type_info&) const override { return nullptr; }
    void check() const override { for (const auto& object : objects) object->check(); }
};
template<class T> void Box<T>::check() const {
    const auto state = context.lock();
    if (!state || !state->alive) throw std::runtime_error("C++ script context has expired");
    if (parent) parent->check();
    if (state->validate && !owned) state->validate(*this);
    if (!get()) throw std::runtime_error("C++ object no longer exists");
}

template<class T> Value view(std::function<T*()> get, const std::shared_ptr<Context>& context,
    std::shared_ptr<Native> parent = {}, bool owned = false) {
    using U = std::remove_cv_t<T>;
    auto native = std::make_shared<Box<T>>();
    native->get = std::move(get); native->context = context; native->parent = std::move(parent);
    native->readOnly = std::is_const_v<T>; native->owned = owned;
    if constexpr (std::is_enum_v<U>) {
        auto result = std::make_shared<Object>(); result->native = native; return result;
    } else {
        const auto found = context->types.find(typeid(U));
        if (found == context->types.end()) throw std::runtime_error("C++ type is outside the reflected API");
        return found->second.view(native, context);
    }
}

template<class R, class... A> std::function<R(A...)> callback(
    const Value& value, const std::shared_ptr<Context>& context, std::type_identity<std::function<R(A...)>>) {
    if (value.isNull()) return {};
    const auto function = value.callable();
    return [function, weak = std::weak_ptr<Context>(context)](A... values) -> R {
        const auto state = weak.lock();
        if (state && state->alive) {
            try {
                Arguments args; args.values = {pack(values, state)...};
                args.modes.resize(sizeof...(A), ArgumentMode::Value);
                const Value result = function->function(args);
                if constexpr (std::is_void_v<R>) return;
                else return convert<R>(result, state);
            } catch (const std::exception& error) {
                if (state->onError) state->onError(error.what()); else throw;
            }
        }
        if constexpr (std::is_reference_v<R>) throw std::runtime_error("Inactive callback cannot return a C++ reference");
        else if constexpr (!std::is_void_v<R>) return R{};
    };
}

template<class T> T convert(const Value& value, const std::shared_ptr<Context>& context) {
    using U = std::remove_cv_t<T>;
    if constexpr (std::same_as<U, Value>) return value;
    else if constexpr (std::same_as<U, std::string>) return value.string();
    else if constexpr (std::same_as<U, bool>) return value.boolean();
    else if constexpr (std::is_arithmetic_v<U>) {
        const double number = value.number();
        if (!std::isfinite(number) || static_cast<long double>(number) < std::numeric_limits<U>::lowest() ||
            static_cast<long double>(number) > std::numeric_limits<U>::max() ||
            (std::is_integral_v<U> && std::floor(number) != number)) throw std::runtime_error("C++ number is out of range");
        return static_cast<U>(number);
    } else if constexpr (std::is_enum_v<U>) {
        if (const auto object = std::get_if<std::shared_ptr<Object>>(&value.data); object && *object && (*object)->native) {
            return *pointer<const U>(value);
        }
        template for (constexpr auto enumerator : std::define_static_array(std::meta::enumerators_of(^^U))) {
            constexpr auto name = std::meta::identifier_of(enumerator);
            constexpr U item = [:enumerator:];
            if (std::holds_alternative<std::string>(value.data)) { if (value.string() == name) return item; }
            else if (value.number() == static_cast<double>(item)) return item;
        }
        throw std::runtime_error("Unknown C++ enum value");
    } else if constexpr (std::is_pointer_v<U> && std::is_class_v<std::remove_pointer_t<U>>) {
        return pointer<std::remove_pointer_t<U>>(value);
    } else if constexpr (Callback<U>::value) return callback(value, context, std::type_identity<U>{});
    else if constexpr (Shared<U>::value) {
        using E = typename Shared<U>::Element;
        if (value.isNull()) return {};
        return U(value.object()->native, pointer<E>(value));
    } else if constexpr (Sequence<U>) {
        if constexpr (std::is_copy_constructible_v<typename U::value_type>) {
            U result{};
            const auto array = value.array();
            if constexpr (requires { result.resize(array->values.size()); }) result.resize(array->values.size());
            else if (result.size() != array->values.size()) throw std::runtime_error("Incorrect C++ array size");
            std::size_t i = 0;
            for (auto&& item : result) item = convert<typename U::value_type>(array->values[i++], context);
            return result;
        } else throw std::runtime_error("C++ collection elements cannot be copied");
    } else if constexpr (std::is_class_v<U> && std::is_copy_constructible_v<U>) return *pointer<const U>(value);
    else throw std::runtime_error("Unsupported C++ value conversion");
}

template<class T> Value pack(T&& value, const std::shared_ptr<Context>& context, std::shared_ptr<Native> parent) {
    using U = std::remove_cvref_t<T>;
    if constexpr (std::same_as<U, Value>) return value;
    else if constexpr (std::same_as<U, bool> || std::same_as<U, std::string>) return Value(value);
    else if constexpr (std::is_arithmetic_v<U>) return Value(static_cast<double>(value));
    else if constexpr (std::is_pointer_v<U> && std::is_class_v<std::remove_pointer_t<U>>) {
        if (!value) return {};
        using E = std::remove_pointer_t<U>;
        return view<E>([value] { return value; }, context, std::move(parent));
    } else if constexpr (Shared<U>::value || Unique<U>::value) {
        if (!value) return {};
        using E = typename U::element_type;
        if constexpr (Shared<U>::value) return view<E>([value] { return value.get(); }, context, {}, true);
        else return pack(value.get(), context, std::move(parent));
    } else if constexpr (Sequence<U>) {
        auto result = std::make_shared<Array>();
        for (auto&& item : value) {
            if constexpr (std::same_as<typename U::value_type, bool>) result->values.emplace_back(static_cast<bool>(item));
            else if constexpr (std::is_lvalue_reference_v<T>) result->values.push_back(pack(item, context, parent));
            else result->values.push_back(pack(std::move(item), context));
        }
        return result;
    } else if constexpr (Callback<U>::value) {
        return value ? packCallback(value, context, std::move(parent)) : Value{};
    } else if constexpr (std::is_class_v<U> || std::is_enum_v<U>) {
        if constexpr (std::is_lvalue_reference_v<T>) {
            using E = std::remove_reference_t<T>;
            return view<E>([address = &value] { return address; }, context, std::move(parent));
        } else if constexpr (std::is_move_constructible_v<U>) {
            auto storage = std::make_shared<U>(std::forward<T>(value));
            return view<U>([storage] { return storage.get(); }, context, {}, true);
        } else throw std::runtime_error("C++ result cannot be moved");
    } else throw std::runtime_error("Unsupported C++ result type");
}

template<class F> Value cachedMember(F&& value, const std::shared_ptr<Context>& context,
    const std::shared_ptr<Native>& parent, Value& cached, const void*& cachedAddress) {
    using U = std::remove_cvref_t<F>;
    constexpr bool referenceView = std::is_class_v<U> && !std::same_as<U, std::string> &&
        !Sequence<U> && !Callback<U>::value && !Shared<U>::value && !Unique<U>::value;
    constexpr bool pointerView = std::is_pointer_v<U> && std::is_class_v<std::remove_pointer_t<U>>;
    if constexpr (referenceView || pointerView) {
        const void* address;
        if constexpr (pointerView) address = value;
        else address = &value;
        if (!address) { cached = {}; cachedAddress = nullptr; return {}; }
        if (cached.isNull() || cachedAddress != address) {
            cached = pack(std::forward<F>(value), context, parent); cachedAddress = address;
        }
        return cached;
    } else {
        // Scalars stay live; collections remain snapshots and owning pointers are not retained.
        return pack(std::forward<F>(value), context, parent);
    }
}

template<class A> struct Slot {
    using U = std::remove_cvref_t<A>;
    static constexpr bool inlineValue = std::is_arithmetic_v<U> && !std::is_lvalue_reference_v<A>;
    std::conditional_t<inlineValue, std::optional<U>, std::monostate> inlineStorage;
    std::shared_ptr<U> storage;
    U* address = nullptr;
    Slot(const Value& value, ArgumentMode mode, const std::shared_ptr<Context>& context) {
        if (mode == ArgumentMode::Out) {
            if constexpr (std::is_default_constructible_v<U>) storage = std::make_shared<U>();
            else throw std::runtime_error("C++ out argument cannot be initialized");
            address = storage.get();
        } else if constexpr (std::is_lvalue_reference_v<A> && std::is_class_v<U> && !std::same_as<U, std::string> && !Sequence<U> && !Callback<U>::value) {
            using E = std::remove_reference_t<A>;
            address = const_cast<U*>(pointer<E>(value));
            if (!address) throw std::runtime_error("C++ references cannot be null");
        } else if constexpr (std::is_constructible_v<U, U&&>) {
            if constexpr (inlineValue) inlineStorage.emplace(convert<U>(value, context));
            else { storage = std::make_shared<U>(convert<U>(value, context)); address = storage.get(); }
        } else throw std::runtime_error("Unsupported C++ parameter");
        if constexpr (std::is_lvalue_reference_v<A> && !std::is_const_v<std::remove_reference_t<A>> &&
            (!std::is_class_v<U> || std::same_as<U, std::string>)) {
            if (mode != ArgumentMode::Ref && mode != ArgumentMode::Out) throw std::runtime_error("C++ parameter requires ref/out");
        } else if (mode == ArgumentMode::Ref || mode == ArgumentMode::Out) {
            if constexpr (!std::is_lvalue_reference_v<A> || std::is_const_v<std::remove_reference_t<A>>) {
                throw std::runtime_error("C++ parameter cannot accept ref/out");
            }
        }
    }
    decltype(auto) get() {
        if constexpr (inlineValue) return static_cast<U&&>(*inlineStorage);
        else if constexpr (std::is_lvalue_reference_v<A>) return static_cast<A>(*address);
        else return static_cast<U&&>(*address);
    }
};
inline ArgumentMode mode(const Arguments& args, std::size_t i) {
    return i < args.modes.size() ? args.modes[i] : ArgumentMode::Value;
}
template<class A> int score(const Value& value, ArgumentMode argumentMode, const std::shared_ptr<Context>& context) {
    Slot<A> probe(value, argumentMode, context);
    using U = std::remove_cvref_t<A>;
    if constexpr (std::is_arithmetic_v<U>) {
        if (std::holds_alternative<bool>(value.data)) return std::same_as<U, bool> ? 0 : 3;
        if (std::holds_alternative<double>(value.data)) return std::same_as<U, double> ? 0 : (std::same_as<U, bool> ? 3 : 1);
        return 4;
    } else if constexpr (std::is_enum_v<U>) return std::holds_alternative<std::shared_ptr<Object>>(value.data) ? 0 : 2;
    else if constexpr (std::same_as<U, std::string>) return std::holds_alternative<std::string>(value.data) ? 0 : 5;
    else return 0;
}

template<std::meta::info M, std::size_t I>
using Parameter = [:std::meta::type_of(std::meta::parameters_of(M)[I]):];
template<std::meta::info M, std::size_t N, std::size_t... I>
auto slots(Arguments& args, const std::shared_ptr<Context>& context, std::index_sequence<I...>) {
    return std::tuple<Slot<Parameter<M, I>>...>{
        Slot<Parameter<M, I>>(args.values[I], mode(args, I), context)...};
}
template<std::meta::info M, std::size_t... I>
int argumentScore(const Arguments& args, const std::shared_ptr<Context>& context, std::index_sequence<I...>) {
    return (0 + ... + score<Parameter<M, I>>(args.values[I], mode(args, I), context));
}
template<class F> Value result(F&& function, const std::shared_ptr<Context>& context, const std::shared_ptr<Native>& parent) {
    if constexpr (std::is_void_v<std::invoke_result_t<F>>) { function(); return {}; }
    else return pack(function(), context, parent);
}
template<class Tuple> std::shared_ptr<Native> keepAlive(const Arguments& args, const Tuple& slots, const std::shared_ptr<Native>& receiver) {
    auto keeper = std::make_shared<KeepAlive>();
    if (receiver) keeper->objects.push_back(receiver);
    for (const auto& value : args.values) {
        const auto object = std::get_if<std::shared_ptr<Object>>(&value.data);
        if (object && *object && (*object)->native) keeper->objects.push_back((*object)->native);
    }
    std::apply([&](const auto&... slot) { ([&] { if (slot.storage) keeper->storage.push_back(slot.storage); }(), ...); }, slots);
    return keeper;
}
template<class F, class Tuple> Value callResult(F&& function, const std::shared_ptr<Context>& context,
    const Arguments& args, const Tuple& slots, const std::shared_ptr<Native>& receiver) {
    using R = std::invoke_result_t<F>;
    if constexpr (std::is_reference_v<R> || std::is_pointer_v<R>)
        return result(std::forward<F>(function), context, keepAlive(args, slots, receiver));
    else return result(std::forward<F>(function), context, {});
}
template<class S> void writeBack(Value& value, S& slot, const std::shared_ptr<Context>& context) {
    if (slot.storage) value = pack(std::move(*slot.storage), context);
    else value = pack(*slot.address, context, value.object()->native);
}
template<class F, std::size_t... I> Value callCallback(F& function, Arguments& args,
    const std::shared_ptr<Context>& context, const std::shared_ptr<Native>& parent, std::index_sequence<I...>) {
    using Parameters = typename Callback<F>::Parameters;
    auto values = std::tuple<Slot<std::tuple_element_t<I, Parameters>>...>{
        Slot<std::tuple_element_t<I, Parameters>>(args.values[I], mode(args, I), context)...};
    const Value returned = callResult([&]() -> decltype(auto) { return function(std::get<I>(values).get()...); }, context, args, values, parent);
    ([&] {
        if (mode(args, I) == ArgumentMode::Ref || mode(args, I) == ArgumentMode::Out)
            writeBack(args.values[I], std::get<I>(values), context);
    }(), ...);
    return returned;
}
template<class F> Value packCallback(F function, const std::shared_ptr<Context>& context, std::shared_ptr<Native> parent) {
    return Value(Function([function, context, parent](Arguments& args) mutable {
        if (!context->alive) throw std::runtime_error("C++ script context has expired");
        if (parent) parent->check();
        constexpr auto count = std::tuple_size_v<typename Callback<F>::Parameters>;
        args.requireCount(count);
        if (!args.typeNames.empty()) throw std::runtime_error("C++ callback is not a template");
        return callCallback(function, args, context, parent, std::make_index_sequence<count>{});
    }));
}

template<std::meta::info M, class T, std::size_t... I>
Value invoke(Arguments& args, const std::shared_ptr<Context>& context, const std::shared_ptr<Native>& native,
    std::index_sequence<I...> indices) {
    auto values = slots<M, sizeof...(I)>(args, context, indices);
    Value returned;
    if constexpr (std::meta::is_constructor(M)) {
        if constexpr (!std::is_abstract_v<T>) {
            auto storage = std::make_shared<T>(std::get<I>(values).get()...);
            returned = view<T>([storage] { return storage.get(); }, context, {}, true);
        }
    } else if constexpr (std::meta::is_class_member(M) && !std::meta::is_static_member(M)) {
        native->check();
        if constexpr (std::meta::is_const(M)) {
            const auto* object = static_cast<const T*>(native->address(typeid(T)));
            returned = callResult([&]() -> decltype(auto) { return object->[:M:](std::get<I>(values).get()...); }, context, args, values, native);
        } else {
            if (native->readOnly) throw std::runtime_error("C++ object is read-only");
            auto* object = static_cast<T*>(const_cast<void*>(native->address(typeid(T))));
            returned = callResult([&]() -> decltype(auto) { return object->[:M:](std::get<I>(values).get()...); }, context, args, values, native);
        }
    } else returned = callResult([&]() -> decltype(auto) { return [:M:](std::get<I>(values).get()...); }, context, args, values, native);
    ([&] {
        if (mode(args, I) == ArgumentMode::Ref || mode(args, I) == ArgumentMode::Out)
            writeBack(args.values[I], std::get<I>(values), context);
    }(), ...);
    return returned;
}

template<std::meta::info M, class T, std::size_t N>
Candidate candidate(const std::shared_ptr<Context>& context, std::shared_ptr<Native> native, std::string generic = {}) {
    return {
        [context, native, generic](const Arguments& args) {
            constexpr bool isConst = std::meta::is_const(M);
            if (!context->alive) return -1;
            if (args.values.size() != N) return -1;
            if (generic.empty() ? !args.typeNames.empty() : args.typeNames.size() != 1) return -1;
            if (!generic.empty()) {
                const auto type = typeName(args.typeNames[0]);
                const auto expected = type.find('.') == std::string::npos ? generic.substr(generic.find_last_of('.') + 1) : generic;
                if (type != expected) return -1;
            }
            if constexpr (std::meta::is_class_member(M) && !std::meta::is_static_member(M) && !std::meta::is_constructor(M)) {
                if (native->readOnly && !isConst) return -1;
            }
            try { return argumentScore<M>(args, context, std::make_index_sequence<N>{}) +
                (native && !native->readOnly && isConst ? 1 : 0); }
            catch (const std::exception&) { return -1; }
        },
        [context, native](Arguments& args) { return invoke<M, T>(args, context, native, std::make_index_sequence<N>{}); },
        N, generic
    };
}
template<std::size_t N> consteval auto indices() {
    std::array<std::size_t, N + 1> values{};
    for (std::size_t i = 0; i <= N; ++i) values[i] = i;
    return values;
}
template<std::meta::info M, class T> void add(Overloads& overloads, const std::string& name,
    const std::shared_ptr<Context>& context, std::shared_ptr<Native> native = {}, std::string generic = {}) {
    if constexpr (!std::meta::is_deleted(M) && !std::meta::is_vararg_function(M) &&
        !std::meta::is_rvalue_reference_qualified(M)) {
        constexpr auto parameters = std::define_static_array(std::meta::parameters_of(M));
        constexpr std::size_t minimum = [] consteval {
            constexpr auto parameters = std::define_static_array(std::meta::parameters_of(M));
            std::size_t n = parameters.size();
            while (n && std::meta::has_default_argument(parameters[n - 1])) --n;
            return n;
        }();
        template for (constexpr std::size_t n : indices<parameters.size()>()) {
            if constexpr (n >= minimum) overloads[name].push_back(candidate<M, T, n>(context, native, generic));
        }
    }
}
inline void install(const std::shared_ptr<Object>& object, Overloads overloads) {
    for (auto& [name, methods] : overloads) {
        object->fields[name] = Value(overloadFunction(std::move(methods)));
    }
}
template<std::meta::info M> void staticField(const std::shared_ptr<Object>& object, const std::shared_ptr<Context>& context) {
    constexpr auto name = std::meta::identifier_of(M);
    using F = [:std::meta::type_of(M):];
    object->property(std::string(name), [context] {
        if (!context->alive) throw std::runtime_error("C++ script context has expired");
        return pack([:M:], context);
    }, [context](const Value& value) {
        if (!context->alive) throw std::runtime_error("C++ script context has expired");
        if constexpr (std::is_assignable_v<F&, F>) [:M:] = convert<F>(value, context);
        else throw std::runtime_error("C++ variable is read-only");
    });
}

template<class T, std::meta::info Scope> void members(const std::shared_ptr<Object>& object,
    const std::shared_ptr<Native>& native, const std::shared_ptr<Context>& context, Overloads& overloads) {
    template for (constexpr auto base : std::define_static_array(std::meta::bases_of(^^T, access))) {
        if constexpr (std::meta::is_public(base)) {
            using B = [:std::meta::type_of(base):];
            members<B, Scope>(object, native, context, overloads);
        }
    }
    std::unordered_set<std::string> declared;
    template for (constexpr auto m : std::define_static_array(std::meta::members_of(^^T, access))) {
        if constexpr (std::meta::is_nonstatic_data_member(m) && !std::meta::is_bit_field(m)) {
            constexpr auto name = std::meta::identifier_of(m);
            using F = [:std::meta::type_of(m):];
            using V = std::remove_cvref_t<F>;
            object->property(std::string(name), [native, context, cached = Value{}, address = static_cast<const void*>(nullptr)]() mutable {
                native->check();
                if (native->readOnly) {
                    const auto* owner = static_cast<const T*>(native->address(typeid(T)));
                    return cachedMember(owner->[:m:], context, native, cached, address);
                }
                auto* owner = static_cast<T*>(const_cast<void*>(native->address(typeid(T))));
                return cachedMember(owner->[:m:], context, native, cached, address);
            }, [native, context](const Value& value) {
                native->check();
                if (native->readOnly) throw std::runtime_error("C++ object is read-only");
                if constexpr (std::is_assignable_v<F&, V> && !Unique<V>::value) {
                    auto* owner = static_cast<T*>(const_cast<void*>(native->address(typeid(T))));
                    owner->[:m:] = convert<V>(value, context);
                } else throw std::runtime_error("C++ field cannot be assigned");
            });
        } else if constexpr (std::meta::is_function(m) && std::meta::has_identifier(m) && !std::meta::is_special_member_function(m) &&
            !std::meta::is_operator_function(m) && !std::meta::is_conversion_function(m)) {
            constexpr auto name = std::meta::identifier_of(m);
            if (declared.insert(std::string(name)).second) overloads.erase(std::string(name));
            add<m, T>(overloads, std::string(name), context, native);
        } else if constexpr (std::meta::is_variable(m) && std::meta::is_static_member(m)) {
            staticField<m>(object, context);
        } else if constexpr (std::meta::is_function_template(m) && std::meta::has_identifier(m) && !std::meta::is_operator_function_template(m) && Scope != std::meta::info{}) {
            constexpr auto name = std::meta::identifier_of(m);
            if (declared.insert(std::string(name)).second) overloads.erase(std::string(name));
            template for (constexpr auto type : std::define_static_array(std::meta::members_of(Scope, access))) {
                if constexpr (std::meta::is_type(type) && std::meta::is_complete_type(type)) {
                    if constexpr (std::meta::can_substitute(m, {type})) {
                        constexpr auto method = std::meta::substitute(m, {type});
                        constexpr auto typeName = std::meta::identifier_of(type);
                        constexpr auto scopeName = std::meta::display_string_of(Scope);
                        add<method, T>(overloads, std::string(name), context, native,
                            reflection_detail::typeName(std::string(scopeName) + "." + std::string(typeName)));
                    }
                }
            }
        }
    }
}

template<class T, std::meta::info Scope> void registerClass(const std::shared_ptr<Context>& context) {
    context->types[typeid(T)].view = [](const auto& native, const auto& state) {
        auto object = std::make_shared<Object>(); object->native = native;
        Overloads overloads; members<T, Scope>(object, native, state, overloads);
        install(object, std::move(overloads)); return Value(object);
    };
}
template<class T, std::size_t I>
using Field = [:std::meta::type_of(std::meta::nonstatic_data_members_of(^^T, access)[I]):];
template<class T, std::size_t... I> Value constructAggregate(Arguments& args,
    const std::shared_ptr<Context>& context, std::index_sequence<I...>) {
    auto slots = std::tuple<Slot<Field<T, I>>...>{Slot<Field<T, I>>(args.values[I], ArgumentMode::Value, context)...};
    auto lifetime = keepAlive(args, slots, {});
    auto storage = std::shared_ptr<T>(new T(std::get<I>(slots).get()...));
    return view<T>([storage] { return storage.get(); }, context, lifetime, true);
}
template<class T, std::size_t N> Candidate aggregate(const std::shared_ptr<Context>& context) {
    return {
        [context](const Arguments& args) {
            if (!context->alive || !args.typeNames.empty() || args.values.size() != N) return -1;
            int total = 0;
            try {
                template for (constexpr auto i : indices<N>()) {
                    if constexpr (i < N) total += score<Field<T, i>>(args.values[i], ArgumentMode::Value, context);
                }
                return total;
            } catch (const std::exception&) { return -1; }
        },
        [context](Arguments& args) {
            return constructAggregate<T>(args, context, std::make_index_sequence<N>{});
        }, N, {}
    };
}
} // namespace reflection_detail

// Compile-time API discovery; runtime values remain independent of the host engine.
class Reflection {
public:
    Reflection() : context_(std::make_shared<reflection_detail::Context>()) {}
    ~Reflection() { context_->alive = false; context_->validate = {}; context_->onError = {}; }
    Reflection(const Reflection&) = delete;
    Reflection& operator=(const Reflection&) = delete;
    void validate(std::function<void(const Object::Native&)> function) { context_->validate = std::move(function); }
    void onError(std::function<void(const std::string&)> function) { context_->onError = std::move(function); }

    template<class T> Value object(T* value) {
        using U = std::remove_cv_t<T>;
        if (!context_->types.contains(typeid(U))) reflection_detail::registerClass<U, std::meta::info{}>(context_);
        return reflection_detail::pack(value, context_);
    }

    // Select existing compiled functions, including standard-library overloads. Names/signatures are reflected.
    template<std::meta::info... Functions> void bindFunctions(Runtime& runtime) {
        reflection_detail::Overloads overloads;
        ([&] {
            static_assert(std::meta::is_function(Functions) && std::meta::has_identifier(Functions));
            constexpr auto name = std::meta::identifier_of(Functions);
            reflection_detail::add<Functions, void>(overloads, std::string(name), context_);
        }(), ...);
        auto object = std::make_shared<Object>();
        reflection_detail::install(object, std::move(overloads));
        for (auto& [name, value] : object->fields) runtime.bind(name, std::move(value));
    }

    template<std::meta::info Scope> void bindNamespace(Runtime& runtime) {
        using namespace reflection_detail;
        static_assert(std::meta::is_namespace(Scope));
        auto space = std::make_shared<Object>(); Overloads functions;
        template for (constexpr auto m : std::define_static_array(std::meta::members_of(Scope, access))) {
            if constexpr (std::meta::is_type(m) && std::meta::is_complete_type(m)) {
                using T = [:m:];
                constexpr auto name = std::meta::identifier_of(m);
                if constexpr (std::is_enum_v<T>) {
                    auto values = std::make_shared<Object>();
                    template for (constexpr auto e : std::define_static_array(std::meta::enumerators_of(m))) {
                        constexpr auto itemName = std::meta::identifier_of(e);
                        values->fields[std::string(itemName)] = pack(T([:e:]), context_);
                    }
                    space->fields[std::string(name)] = values;
                } else if constexpr (std::is_class_v<T>) {
                    registerClass<T, Scope>(context_);
                    auto type = std::make_shared<Object>(); Overloads methods, constructors;
                    template for (constexpr auto f : std::define_static_array(std::meta::members_of(m, access))) {
                        if constexpr (std::meta::is_function(f) && std::meta::is_static_member(f) && !std::meta::is_operator_function(f)) {
                            constexpr auto methodName = std::meta::identifier_of(f);
                            add<f, T>(methods, std::string(methodName), context_);
                        } else if constexpr (std::meta::is_variable(f) && std::meta::is_static_member(f)) {
                            staticField<f>(type, context_);
                        } else if constexpr (std::meta::is_constructor(f) && !std::meta::is_copy_constructor(f) &&
                            !std::meta::is_move_constructor(f) && !std::is_abstract_v<T> &&
                            !(std::is_aggregate_v<T> && std::meta::bases_of(m, access).empty())) {
                            add<f, T>(constructors, std::string(name), context_);
                        }
                    }
                    if constexpr (std::is_aggregate_v<T> && std::is_default_constructible_v<T> &&
                        std::meta::bases_of(m, access).empty()) {
                        constexpr auto count = std::meta::nonstatic_data_members_of(m, access).size();
                        template for (constexpr auto n : indices<count>()) constructors[std::string(name)].push_back(aggregate<T, n>(context_));
                    }
                    else if constexpr (std::is_default_constructible_v<T>) {
                        if (constructors[std::string(name)].empty()) {
                            constructors[std::string(name)].push_back({
                                [state = context_](const Arguments& args) { return state->alive && args.values.empty() && args.typeNames.empty() ? 0 : -1; },
                                [state = context_](Arguments&) {
                                    auto storage = std::make_shared<T>();
                                    return view<T>([storage] { return storage.get(); }, state, {}, true);
                                }, 0, {}});
                        }
                    }
                    install(type, std::move(methods)); space->fields[std::string(name)] = type;
                    auto candidates = std::move(constructors[std::string(name)]);
                    constexpr auto scopeName = std::meta::identifier_of(Scope);
                    runtime.constructor(std::string(scopeName) + "." + std::string(name), overloadFunction(std::move(candidates)));
                }
            } else if constexpr (std::meta::is_function(m) && !std::meta::is_operator_function(m)) {
                constexpr auto name = std::meta::identifier_of(m);
                add<m, void>(functions, std::string(name), context_);
            } else if constexpr (std::meta::is_variable(m)) {
                staticField<m>(space, context_);
            }
        }
        install(space, std::move(functions));
        constexpr auto name = std::meta::identifier_of(Scope);
        runtime.bind(std::string(name), space);
    }

private:
    std::shared_ptr<reflection_detail::Context> context_;
};
} // namespace eryscript
