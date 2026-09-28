// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    explicit Value(bool b) : type_(Type::Bool), num_(b ? 1.0 : 0.0) {}
    explicit Value(double n) : type_(Type::Number), num_(n) {}
    explicit Value(const std::string& s) : type_(Type::String), str_(s) {}
    explicit Value(const char* s) : type_(Type::String), str_(s ? s : "") {}

    static Value Object() {
        Value v;
        v.type_ = Type::Object;
        return v;
    }
    static Value Array() {
        Value v;
        v.type_ = Type::Array;
        return v;
    }

    Type GetType() const { return type_; }
    bool IsNull() const { return type_ == Type::Null; }
    bool IsBool() const { return type_ == Type::Bool; }
    bool IsNumber() const { return type_ == Type::Number; }
    bool IsString() const { return type_ == Type::String; }
    bool IsArray() const { return type_ == Type::Array; }
    bool IsObject() const { return type_ == Type::Object; }

    bool AsBool(bool def = false) const;
    double AsNumber(double def = 0.0) const;
    std::string AsString(const std::string& def = std::string()) const;

    const Value* Find(const std::string& key) const;
    void Set(const std::string& key, Value v);
    const std::vector<std::pair<std::string, Value>>& Members() const { return members_; }

    void Push(Value v);
    const std::vector<Value>& Elements() const { return elements_; }

    static bool Parse(const std::string& text, Value& out, std::string& error, int& line);

    std::string Serialize() const;

private:
    void SerializeTo(std::string& out, int depth) const;

    Type type_ = Type::Null;
    double num_ = 0.0;
    std::string str_;
    std::vector<std::pair<std::string, Value>> members_;
    std::vector<Value> elements_;
};

}  // namespace json
