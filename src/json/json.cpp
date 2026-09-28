// SPDX-License-Identifier: MIT
#include "json/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace json {
namespace {

class Parser {
public:
    Parser(const char* p) : p_(p) {}

    bool ParseValue(Value& out);
    const std::string& Error() const { return error_; }
    int Line() const { return line_; }

private:
    void SkipWhitespace();
    bool Fail(const char* what);
    bool ParseString(std::string& out);
    bool ParseNumber(Value& out);
    bool ParseObject(Value& out);
    bool ParseArray(Value& out);
    bool ParseLiteral(const char* text, Value v, Value& out);

    static void AppendUtf8(std::string& out, unsigned int cp);

    const char* p_;
    int line_ = 1;
    std::string error_;
    int depth_ = 0;
};

constexpr int kMaxDepth = 32;

void Parser::SkipWhitespace() {
    for (;;) {
        while (*p_ == ' ' || *p_ == '\t' || *p_ == '\r') ++p_;
        if (*p_ == '\n') {
            ++line_;
            ++p_;
            continue;
        }
        return;
    }
}

bool Parser::Fail(const char* what) {
    if (error_.empty()) {
        char buf[160];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s at line %d", what, line_);
        error_ = buf;
    }
    return false;
}

void Parser::AppendUtf8(std::string& out, unsigned int cp) {
    if (cp < 0x80) {
        out.push_back((char)cp);
    } else if (cp < 0x800) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

bool ReadHex4(const char*& p, unsigned int& out) {
    out = 0;
    for (int i = 0; i < 4; ++i) {
        const char c = p[i];
        unsigned int d;
        if (c >= '0' && c <= '9') d = (unsigned int)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned int)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (unsigned int)(c - 'A' + 10);
        else return false;
        out = (out << 4) | d;
    }
    p += 4;
    return true;
}

bool Parser::ParseString(std::string& out) {
    if (*p_ != '"') return Fail("expected a string");
    ++p_;
    out.clear();
    for (;;) {
        const char c = *p_;
        if (c == '\0') return Fail("unterminated string");
        if (c == '\n') return Fail("unterminated string");
        if (c == '"') {
            ++p_;
            return true;
        }
        if (c != '\\') {
            out.push_back(c);
            ++p_;
            continue;
        }
        ++p_;
        switch (*p_) {
            case '"': out.push_back('"'); ++p_; break;
            case '\\': out.push_back('\\'); ++p_; break;
            case '/': out.push_back('/'); ++p_; break;
            case 'b': out.push_back('\b'); ++p_; break;
            case 'f': out.push_back('\f'); ++p_; break;
            case 'n': out.push_back('\n'); ++p_; break;
            case 'r': out.push_back('\r'); ++p_; break;
            case 't': out.push_back('\t'); ++p_; break;
            case 'u': {
                ++p_;
                unsigned int cp = 0;
                if (!ReadHex4(p_, cp)) return Fail("bad \\u escape");

                if (cp >= 0xD800 && cp <= 0xDBFF && p_[0] == '\\' && p_[1] == 'u') {
                    const char* save = p_;
                    p_ += 2;
                    unsigned int lo = 0;
                    if (ReadHex4(p_, lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else {
                        p_ = save;
                    }
                }
                AppendUtf8(out, cp);
                break;
            }
            default: return Fail("bad escape sequence");
        }
    }
}

bool Parser::ParseNumber(Value& out) {

    if (*p_ != '-' && (*p_ < '0' || *p_ > '9')) return Fail("expected a number");
    char* end = nullptr;
    const double n = strtod(p_, &end);
    if (end == p_) return Fail("expected a number");
    if (!std::isfinite(n)) return Fail("number out of range");
    p_ = end;
    out = Value(n);
    return true;
}

bool Parser::ParseLiteral(const char* text, Value v, Value& out) {
    const size_t len = strlen(text);
    if (strncmp(p_, text, len) != 0) return Fail("unexpected character");
    p_ += len;
    out = v;
    return true;
}

bool Parser::ParseArray(Value& out) {
    ++p_;
    out = Value::Array();
    SkipWhitespace();
    if (*p_ == ']') {
        ++p_;
        return true;
    }
    for (;;) {
        Value element;
        if (!ParseValue(element)) return false;
        out.Push(element);
        SkipWhitespace();
        if (*p_ == ',') {
            ++p_;
            SkipWhitespace();
            continue;
        }
        if (*p_ == ']') {
            ++p_;
            return true;
        }
        return Fail("expected ',' or ']'");
    }
}

bool Parser::ParseObject(Value& out) {
    ++p_;
    out = Value::Object();
    SkipWhitespace();
    if (*p_ == '}') {
        ++p_;
        return true;
    }
    for (;;) {
        SkipWhitespace();
        std::string key;
        if (!ParseString(key)) return false;
        SkipWhitespace();
        if (*p_ != ':') return Fail("expected ':'");
        ++p_;
        SkipWhitespace();
        Value member;
        if (!ParseValue(member)) return false;
        out.Set(key, member);
        SkipWhitespace();
        if (*p_ == ',') {
            ++p_;
            continue;
        }
        if (*p_ == '}') {
            ++p_;
            return true;
        }
        return Fail("expected ',' or '}'");
    }
}

bool Parser::ParseValue(Value& out) {
    if (++depth_ > kMaxDepth) return Fail("nested too deeply");
    SkipWhitespace();
    bool ok;
    switch (*p_) {
        case '{': ok = ParseObject(out); break;
        case '[': ok = ParseArray(out); break;
        case '"': {
            std::string s;
            ok = ParseString(s);
            if (ok) out = Value(s);
            break;
        }
        case 't': ok = ParseLiteral("true", Value(true), out); break;
        case 'f': ok = ParseLiteral("false", Value(false), out); break;
        case 'n': ok = ParseLiteral("null", Value(), out); break;
        case '\0': ok = Fail("unexpected end of file"); break;
        default: ok = ParseNumber(out); break;
    }
    --depth_;
    return ok;
}

void EscapeTo(std::string& out, const std::string& s) {
    out.push_back('"');
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "\\u%04x", (unsigned char)c);
                    out += buf;
                } else {

                    out.push_back(c);
                }
                break;
        }
    }
    out.push_back('"');
}

void NumberTo(std::string& out, double n) {
    char buf[40];

    if (n < 1e15 && n > -1e15 && n == (double)(long long)n) {
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%lld", (long long)n);
    } else {
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.10g", n);
    }
    out += buf;
}

void IndentTo(std::string& out, int depth) {
    out.append((size_t)depth * 2, ' ');
}

}  // namespace

bool Value::AsBool(bool def) const {
    if (type_ == Type::Bool) return num_ != 0.0;
    if (type_ == Type::Number) return num_ != 0.0;
    return def;
}

double Value::AsNumber(double def) const {
    if (type_ == Type::Number || type_ == Type::Bool) return num_;
    return def;
}

std::string Value::AsString(const std::string& def) const {
    if (type_ == Type::String) return str_;
    return def;
}

const Value* Value::Find(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& m : members_) {
        if (m.first == key) return &m.second;
    }
    return nullptr;
}

void Value::Set(const std::string& key, Value v) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        members_.clear();
    }
    for (auto& m : members_) {
        if (m.first == key) {
            m.second = std::move(v);
            return;
        }
    }
    members_.emplace_back(key, std::move(v));
}

void Value::Push(Value v) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        elements_.clear();
    }
    elements_.push_back(std::move(v));
}

bool Value::Parse(const std::string& text, Value& out, std::string& error, int& line) {
    Parser parser(text.c_str());
    if (!parser.ParseValue(out)) {
        error = parser.Error();
        line = parser.Line();
        return false;
    }
    error.clear();
    line = parser.Line();
    return true;
}

void Value::SerializeTo(std::string& out, int depth) const {
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += (num_ != 0.0) ? "true" : "false"; break;
        case Type::Number: NumberTo(out, num_); break;
        case Type::String: EscapeTo(out, str_); break;
        case Type::Array: {
            if (elements_.empty()) {
                out += "[]";
                break;
            }
            out += "[\n";
            for (size_t i = 0; i < elements_.size(); ++i) {
                IndentTo(out, depth + 1);
                elements_[i].SerializeTo(out, depth + 1);
                if (i + 1 < elements_.size()) out.push_back(',');
                out.push_back('\n');
            }
            IndentTo(out, depth);
            out.push_back(']');
            break;
        }
        case Type::Object: {
            if (members_.empty()) {
                out += "{}";
                break;
            }
            out += "{\n";
            for (size_t i = 0; i < members_.size(); ++i) {
                IndentTo(out, depth + 1);
                EscapeTo(out, members_[i].first);
                out += ": ";
                members_[i].second.SerializeTo(out, depth + 1);
                if (i + 1 < members_.size()) out.push_back(',');
                out.push_back('\n');
            }
            IndentTo(out, depth);
            out.push_back('}');
            break;
        }
    }
}

std::string Value::Serialize() const {
    std::string out;
    SerializeTo(out, 0);
    out.push_back('\n');
    return out;
}

}  // namespace json
