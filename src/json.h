// Minimal JSON parser (recursive descent). Just enough for the bootset manifest
// format: [["path", [[offset,length], ...]], ...] and general config files.
// No allocations beyond std::string/vector/map. No external deps.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept>

namespace js {

struct Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value>;

struct Value {
    enum Type { Null, Bool, Number, String, ArrayT, ObjectT } type = Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::shared_ptr<Array> arr;
    std::shared_ptr<Object> obj;

    Value() {}
    Value(std::nullptr_t) {}
    Value(bool v) : type(Bool), b(v) {}
    Value(double v) : type(Number), num(v) {}
    Value(const std::string& v) : type(String), str(v) {}
    Value(Array v) : type(ArrayT), arr(std::make_shared<Array>(std::move(v))) {}
    Value(Object v) : type(ObjectT), obj(std::make_shared<Object>(std::move(v))) {}

    bool isNull() const { return type == Null; }
    bool isArray() const { return type == ArrayT; }
    bool isObject() const { return type == ObjectT; }
    bool isString() const { return type == String; }
    bool isNumber() const { return type == Number; }

    const Array& asArray() const { return *arr; }
    const Object& asObject() const { return *obj; }
    const std::string& asString() const { return str; }
    double asNumber() const { return num; }
    int64_t asInt() const { return (int64_t)num; }
};

// Parses a full JSON document. Throws std::runtime_error on malformed input.
Value parse(const std::string& text);

} // namespace js
