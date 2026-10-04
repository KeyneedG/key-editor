#pragma once

// Small JSON value/parser shared by scene files and editor project settings.
#include <charconv>
#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace tiny3d::json {
struct Value {
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> data = nullptr;
    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool v) : data(v) {}
    Value(double v) : data(v) {}
    Value(float v) : data(double(v)) {}
    Value(int v) : data(double(v)) {}
    Value(std::size_t v) : data(double(v)) {}
    Value(const char* v) : data(std::string(v)) {}
    Value(std::string v) : data(std::move(v)) {}
    Value(Array v) : data(std::move(v)) {}
    Value(Object v) : data(std::move(v)) {}
    bool null() const { return std::holds_alternative<std::nullptr_t>(data); }
    const Array& array() const { return get<Array>(); }
    const Object& object() const { return get<Object>(); }
    std::string string() const { return get<std::string>(); }
    double number() const { return get<double>(); }
    bool boolean() const { return get<bool>(); }
    const Value& at(const std::string& key) const {
        const auto& o = object(); const auto found = o.find(key);
        if (found == o.end()) throw std::runtime_error("Missing JSON member: " + key);
        return found->second;
    }
    const Value& at(std::size_t i) const { return array().at(i); }
    bool has(const std::string& key) const { return object().contains(key); }
    Value& operator[](const std::string& key) { return std::get<Object>(data)[key]; }
private:
    template<class T> const T& get() const {
        const auto* v = std::get_if<T>(&data);
        if (!v) throw std::runtime_error("Wrong JSON value type");
        return *v;
    }
};

inline void appendUtf8(std::string& s, unsigned c) {
    if (c < 0x80) s += char(c);
    else if (c < 0x800) { s += char(0xc0 | (c >> 6)); s += char(0x80 | (c & 63)); }
    else if (c < 0x10000) { s += char(0xe0 | (c >> 12)); s += char(0x80 | ((c >> 6) & 63)); s += char(0x80 | (c & 63)); }
    else { s += char(0xf0 | (c >> 18)); s += char(0x80 | ((c >> 12) & 63)); s += char(0x80 | ((c >> 6) & 63)); s += char(0x80 | (c & 63)); }
}

class Parser {
    std::string_view s_; std::size_t i_ = 0;
    [[noreturn]] void error() const { throw std::runtime_error("Invalid JSON at byte " + std::to_string(i_)); }
    void space() { while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\r' || s_[i_] == '\n' || s_[i_] == '\t')) ++i_; }
    bool take(char c) { space(); if (i_ < s_.size() && s_[i_] == c) { ++i_; return true; } return false; }
    unsigned hex() {
        unsigned n = 0;
        for (int j = 0; j < 4; ++j) {
            if (i_ == s_.size()) error();
            char c = s_[i_++]; n *= 16;
            if (c >= '0' && c <= '9') n += c - '0';
            else if (c >= 'a' && c <= 'f') n += c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') n += c - 'A' + 10;
            else error();
        }
        return n;
    }
    std::string quoted() {
        if (!take('"')) error();
        std::string result;
        while (i_ < s_.size()) {
            char c = s_[i_++];
            if (c == '"') return result;
            if (static_cast<unsigned char>(c) < 32) error();
            if (c != '\\') { result += c; continue; }
            if (i_ == s_.size()) error();
            switch (s_[i_++]) {
            case '"': result += '"'; break;
            case '\\': result += '\\'; break;
            case '/': result += '/'; break;
            case 'b': result += '\b'; break;
            case 'f': result += '\f'; break;
            case 'n': result += '\n'; break;
            case 'r': result += '\r'; break;
            case 't': result += '\t'; break;
            case 'u': {
                unsigned code = hex();
                if (code >= 0xd800 && code <= 0xdbff) {
                    if (i_ + 2 > s_.size() || s_.substr(i_, 2) != "\\u") error();
                    i_ += 2; unsigned low = hex();
                    if (low < 0xdc00 || low > 0xdfff) error();
                    code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                } else if (code >= 0xdc00 && code <= 0xdfff) error();
                appendUtf8(result, code); break;
            }
            default: error();
            }
        }
        error();
    }
    Value value(unsigned depth) {
        if (depth > 128) error();
        space(); if (i_ == s_.size()) error();
        if (s_[i_] == '"') return quoted();
        if (take('{')) {
            Value::Object o;
            if (!take('}')) do {
                auto key = quoted(); if (!take(':')) error();
                if (!o.emplace(key, value(depth + 1)).second) error();
                if (take('}')) return o;
            } while (take(',')); else return o;
            error();
        }
        if (take('[')) {
            Value::Array a;
            if (!take(']')) do {
                a.push_back(value(depth + 1)); if (take(']')) return a;
            } while (take(',')); else return a;
            error();
        }
        for (auto literal : {"true", "false", "null"}) {
            const std::string_view word = literal;
            if (s_.substr(i_, word.size()) == word) {
                i_ += word.size(); if (word == "null") return {}; return word == "true";
            }
        }
        std::size_t start = i_;
        if (s_[i_] == '-') ++i_;
        if (i_ == s_.size()) error();
        if (s_[i_] == '0') ++i_;
        else {
            if (s_[i_] < '1' || s_[i_] > '9') error();
            while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
        }
        const auto digits = [&] {
            auto first = i_; while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
            if (i_ == first) error();
        };
        if (i_ < s_.size() && s_[i_] == '.') { ++i_; digits(); }
        if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
            ++i_; if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_; digits();
        }
        double n = 0; auto parsed = std::from_chars(s_.data() + start, s_.data() + i_, n);
        if (parsed.ec != std::errc{} || !std::isfinite(n)) error();
        return n;
    }
public:
    explicit Parser(std::string_view s) : s_(s) {}
    Value parse() { auto v = value(0); space(); if (i_ != s_.size()) error(); return v; }
};
inline Value parse(std::string_view s) { return Parser(s).parse(); }

inline void write(std::ostream& out, const Value& v) {
    if (v.null()) { out << "null"; return; }
    if (auto* b = std::get_if<bool>(&v.data)) { out << (*b ? "true" : "false"); return; }
    if (auto* n = std::get_if<double>(&v.data)) {
        if (!std::isfinite(*n)) throw std::runtime_error("JSON cannot store non-finite numbers");
        out << std::setprecision(17) << *n; return;
    }
    if (auto* s = std::get_if<std::string>(&v.data)) {
        out << '"';
        for (unsigned char c : *s) {
            switch (c) {
            case '"': out << "\\\""; break; case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break; case '\r': out << "\\r"; break; case '\t': out << "\\t"; break;
            default:
                if (c < 32) out << "\\u00" << "0123456789abcdef"[c >> 4] << "0123456789abcdef"[c & 15];
                else out << char(c);
            }
        }
        out << '"'; return;
    }
    bool first = true;
    if (auto* a = std::get_if<Value::Array>(&v.data)) {
        out << '['; for (const auto& item : *a) { if (!first) out << ','; first = false; write(out, item); } out << ']';
    } else {
        out << '{'; for (const auto& [key, item] : v.object()) {
            if (!first) out << ',';
            first = false; write(out, Value(key)); out << ':'; write(out, item);
        } out << '}';
    }
}
inline std::string stringify(const Value& v) { std::ostringstream out; out.imbue(std::locale::classic()); write(out, v); return out.str(); }
} // namespace tiny3d::json
