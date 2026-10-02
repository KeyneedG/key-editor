#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

// Standalone interpreter. No engine, operating-system or Unity dependencies.
namespace eryscript {

struct Object;
struct Array;
struct Callable;
struct Arguments;
class Value;
using Function = std::function<Value(Arguments&)>;

class Value {
public:
    using Storage = std::variant<std::monostate, bool, double, std::string,
        std::shared_ptr<Object>, std::shared_ptr<Array>, std::shared_ptr<Callable>>;
    Storage data;

    Value() = default;
    Value(bool value) : data(value) {}
    Value(int value) : data(double(value)) {}
    Value(float value) : data(double(value)) {}
    Value(double value) : data(value) {}
    Value(const char* value) : data(std::string(value)) {}
    Value(std::string value) : data(std::move(value)) {}
    Value(std::shared_ptr<Object> value) : data(std::move(value)) {}
    Value(std::shared_ptr<Array> value) : data(std::move(value)) {}
    Value(Function function);

    bool isNull() const;
    double number() const;
    bool boolean() const;
    std::string string() const;
    std::shared_ptr<Object> object() const;
    std::shared_ptr<Array> array() const;
    std::shared_ptr<Callable> callable() const;
};

enum class ArgumentMode { Value, Ref, Out, In };
struct Arguments {
    std::vector<Value> values;
    std::vector<ArgumentMode> modes;
    std::vector<std::string> typeNames; // Explicit generic arguments, e.g. GetComponent<Camera>().
    void requireCount(std::size_t count) const;
};
struct Callable {
    Function function;
    bool scriptMethod = false;
};

struct Object {
    struct Native {
        virtual ~Native() = default;
        virtual const void* address(const std::type_info& type) const = 0;
        virtual void check() const = 0;
        virtual const std::type_info& type() const { return typeid(*this); }
        virtual bool equals(const Native& other) const { return this == &other; }
        bool readOnly = false;
        bool owned = false;
    };
    struct Property {
        std::function<Value()> get;
        std::function<void(const Value&)> set; // Empty means read-only.
    };
    std::unordered_map<std::string, Value> fields;
    std::unordered_map<std::string, Property> properties;
    std::shared_ptr<Native> native; // Optional C++ backing, used by reflection.hpp.
    Value get(const std::string& name) const;
    void set(const std::string& name, const Value& value);
    void property(std::string name, std::function<Value()> get,
        std::function<void(const Value&)> set = {});
};

struct Array {
    std::vector<Value> values;
    std::vector<std::size_t> dimensions; // Empty for a resizable List; otherwise a fixed array.
};

struct Spec {
    bool enabled = true, readOnly = false;
    std::string name, id;
};

class Error : public std::runtime_error {
public:
    std::size_t line;
    Error(std::size_t line, const std::string& message);
};

class Runtime {
public:
    Runtime();
    ~Runtime();
    Runtime(Runtime&&) noexcept;
    Runtime& operator=(Runtime&&) noexcept;
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    // Bindings/constructors persist when loading new source; script variables do not.
    void bind(std::string name, Value value);
    void constructor(std::string name, Function function);
    void load(const std::string& source, Value root = {}); // Parse once; execute top-level statements once.
    bool hasMethod(const std::string& name) const;
    Value call(const std::string& name, std::vector<Value> args = {});
    Value get(const std::string& name) const;
    void set(const std::string& name, Value value);
    void stop(); // Previously obtained script callbacks become harmless.
    const Spec& spec() const;
    std::size_t stepLimit = 100000; // Per top-level run / external method invocation.

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace eryscript
