#include "util/Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace vb {
namespace json {
namespace {

constexpr int kMaxDepth = 64;

void AppendUtf8(std::string& out, unsigned int codepoint) {
    if (codepoint <= 0x7Fu) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else if (codepoint <= 0xFFFFu) {
        out.push_back(static_cast<char>(0xE0u | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
}

bool IsDigit(char c) {
    return c >= '0' && c <= '9';
}

std::string FormatNumber(double value) {
    char buffer[64] = {};
    if (std::isfinite(value) && std::floor(value) == value &&
        std::fabs(value) < 9.0e15) {
        ::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
    } else {
        ::snprintf(buffer, sizeof(buffer), "%.10g", value);
    }
    return std::string(buffer);
}

void DumpIndent(std::string& out, int indent, int depth) {
    out.append(static_cast<size_t>(indent) * static_cast<size_t>(depth), ' ');
}

void DumpString(std::string& out, const std::string& text) {
    out.push_back('"');
    for (unsigned char c : text) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20u) {
                char escape[8] = {};
                ::snprintf(escape, sizeof(escape), "\\u%04x", c);
                out += escape;
            } else {
                // 直接输出 UTF-8 字节
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    out.push_back('"');
}

class Parser {
public:
    explicit Parser(const std::string& text)
        : cursor_(text.c_str()), end_(text.c_str() + text.size()) {}

    bool Parse(Value& out) {
        SkipWhitespace();
        if (!ParseValue(out, 0)) {
            return false;
        }
        SkipWhitespace();
        if (cursor_ != end_) {
            error_ = "根节点之后存在多余内容";
            return false;
        }
        return true;
    }

    const std::string& error() const { return error_; }

private:
    void SkipWhitespace() {
        while (cursor_ != end_) {
            const char c = *cursor_;
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++cursor_;
            } else {
                break;
            }
        }
    }

    bool Fail(const std::string& message) {
        if (error_.empty()) {
            error_ = message;
        }
        return false;
    }

    bool ParseValue(Value& out, int depth) {
        if (depth > kMaxDepth) {
            return Fail("JSON 嵌套层级过深");
        }
        SkipWhitespace();
        if (cursor_ == end_) {
            return Fail("内容意外结束");
        }
        const char c = *cursor_;
        switch (c) {
        case '{': return ParseObject(out, depth);
        case '[': return ParseArray(out, depth);
        case '"': {
            std::string text;
            if (!ParseString(text)) {
                return false;
            }
            out = Value(std::move(text));
            return true;
        }
        case 't':
            if (MatchLiteral("true")) {
                out = Value(true);
                return true;
            }
            return Fail("非法字面量");
        case 'f':
            if (MatchLiteral("false")) {
                out = Value(false);
                return true;
            }
            return Fail("非法字面量");
        case 'n':
            if (MatchLiteral("null")) {
                out = Value();
                return true;
            }
            return Fail("非法字面量");
        default:
            return ParseNumber(out);
        }
    }

    bool MatchLiteral(const char* literal) {
        const size_t length = std::strlen(literal);
        if (static_cast<size_t>(end_ - cursor_) < length) {
            return false;
        }
        if (std::memcmp(cursor_, literal, length) != 0) {
            return false;
        }
        cursor_ += length;
        return true;
    }

    bool ParseNumber(Value& out) {
        if (cursor_ == end_) {
            return Fail("缺少数值");
        }
        if (*cursor_ != '-' && !IsDigit(*cursor_)) {
            return Fail("非法字符");
        }
        char* numberEnd = nullptr;
        const double value = std::strtod(cursor_, &numberEnd);
        if (numberEnd == cursor_) {
            return Fail("数值格式非法");
        }
        if (numberEnd != end_) {
            const char next = *numberEnd;
            const bool legal =
                next == ',' || next == '}' || next == ']' || next == ' ' ||
                next == '\t' || next == '\r' || next == '\n';
            if (!legal) {
                return Fail("数值格式非法");
            }
        }
        cursor_ = numberEnd;
        out = Value(value);
        return true;
    }

    bool ParseString(std::string& out) {
        if (cursor_ == end_ || *cursor_ != '"') {
            return Fail("缺少字符串起始引号");
        }
        ++cursor_;
        out.clear();
        while (cursor_ != end_) {
            const unsigned char c = static_cast<unsigned char>(*cursor_);
            if (c == '"') {
                ++cursor_;
                return true;
            }
            if (c < 0x20u) {
                return Fail("字符串中存在控制字符");
            }
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                ++cursor_;
                continue;
            }
            ++cursor_;
            if (cursor_ == end_) {
                return Fail("转义序列不完整");
            }
            const char escape = *cursor_++;
            switch (escape) {
            case '"':  out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/':  out.push_back('/'); break;
            case 'b':  out.push_back('\b'); break;
            case 'f':  out.push_back('\f'); break;
            case 'n':  out.push_back('\n'); break;
            case 'r':  out.push_back('\r'); break;
            case 't':  out.push_back('\t'); break;
            case 'u': {
                unsigned int codepoint = 0;
                if (!ParseHex4(codepoint)) {
                    return false;
                }
                if (codepoint >= 0xD800u && codepoint <= 0xDBFFu) {
                    // 代理对高位，尝试拼接低位
                    if (static_cast<size_t>(end_ - cursor_) >= 6 &&
                        cursor_[0] == '\\' && cursor_[1] == 'u') {
                        cursor_ += 2;
                        unsigned int low = 0;
                        if (!ParseHex4(low)) {
                            return false;
                        }
                        if (low >= 0xDC00u && low <= 0xDFFFu) {
                            codepoint = 0x10000u +
                                        ((codepoint - 0xD800u) << 10) +
                                        (low - 0xDC00u);
                        } else {
                            AppendUtf8(out, 0xFFFDu);
                            codepoint = low;
                        }
                    } else {
                        codepoint = 0xFFFDu;
                    }
                } else if (codepoint >= 0xDC00u && codepoint <= 0xDFFFu) {
                    codepoint = 0xFFFDu;
                }
                AppendUtf8(out, codepoint);
                break;
            }
            default:
                return Fail("不支持的转义序列");
            }
        }
        return Fail("字符串未闭合");
    }

    bool ParseHex4(unsigned int& out) {
        if (static_cast<size_t>(end_ - cursor_) < 4) {
            return Fail("\\u 转义长度不足");
        }
        unsigned int value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = *cursor_++;
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= static_cast<unsigned int>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<unsigned int>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<unsigned int>(c - 'A' + 10);
            } else {
                return Fail("\\u 转义存在非法字符");
            }
        }
        out = value;
        return true;
    }

    bool ParseObject(Value& out, int depth) {
        ++cursor_; // 跳过 '{'
        Value object = Value::MakeObject();
        SkipWhitespace();
        if (cursor_ != end_ && *cursor_ == '}') {
            ++cursor_;
            out = std::move(object);
            return true;
        }
        while (cursor_ != end_) {
            SkipWhitespace();
            std::string key;
            if (!ParseString(key)) {
                return false;
            }
            SkipWhitespace();
            if (cursor_ == end_ || *cursor_ != ':') {
                return Fail("对象成员缺少冒号");
            }
            ++cursor_;
            Value member;
            if (!ParseValue(member, depth + 1)) {
                return false;
            }
            object.Set(key, std::move(member));
            SkipWhitespace();
            if (cursor_ == end_) {
                break;
            }
            if (*cursor_ == ',') {
                ++cursor_;
                continue;
            }
            if (*cursor_ == '}') {
                ++cursor_;
                out = std::move(object);
                return true;
            }
            return Fail("对象成员分隔符非法");
        }
        return Fail("对象未闭合");
    }

    bool ParseArray(Value& out, int depth) {
        ++cursor_; // 跳过 '['
        Value array = Value::MakeArray();
        SkipWhitespace();
        if (cursor_ != end_ && *cursor_ == ']') {
            ++cursor_;
            out = std::move(array);
            return true;
        }
        while (cursor_ != end_) {
            Value element;
            if (!ParseValue(element, depth + 1)) {
                return false;
            }
            array.Push(std::move(element));
            SkipWhitespace();
            if (cursor_ == end_) {
                break;
            }
            if (*cursor_ == ',') {
                ++cursor_;
                continue;
            }
            if (*cursor_ == ']') {
                ++cursor_;
                out = std::move(array);
                return true;
            }
            return Fail("数组元素分隔符非法");
        }
        return Fail("数组未闭合");
    }

    const char* cursor_ = nullptr;
    const char* end_ = nullptr;
    std::string error_;
};

} // namespace

Value Value::MakeObject() {
    Value value;
    value.type_ = Type::Object;
    return value;
}

Value Value::MakeArray() {
    Value value;
    value.type_ = Type::Array;
    return value;
}

bool Value::AsBool(bool fallback) const {
    if (type_ == Type::Bool) {
        return bool_;
    }
    if (type_ == Type::Number) {
        return number_ != 0.0;
    }
    return fallback;
}

double Value::AsNumber(double fallback) const {
    if (type_ == Type::Number) {
        return number_;
    }
    return fallback;
}

int Value::AsInt(int fallback) const {
    if (type_ == Type::Number) {
        return static_cast<int>(number_);
    }
    return fallback;
}

std::string Value::AsString(const std::string& fallback) const {
    if (type_ == Type::String) {
        return string_;
    }
    return fallback;
}

const Value* Value::Find(const std::string& key) const {
    if (type_ != Type::Object) {
        return nullptr;
    }
    for (const auto& member : members_) {
        if (member.first == key) {
            return &member.second;
        }
    }
    return nullptr;
}

Value& Value::Set(const std::string& key, Value value) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        members_.clear();
        elements_.clear();
    }
    for (auto& member : members_) {
        if (member.first == key) {
            member.second = std::move(value);
            return member.second;
        }
    }
    members_.emplace_back(key, std::move(value));
    return members_.back().second;
}

size_t Value::Size() const {
    if (type_ == Type::Array) {
        return elements_.size();
    }
    if (type_ == Type::Object) {
        return members_.size();
    }
    return 0;
}

const Value& Value::At(size_t index) const {
    static const Value kNull;
    if (type_ != Type::Array || index >= elements_.size()) {
        return kNull;
    }
    return elements_[index];
}

void Value::Push(Value value) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        elements_.clear();
        members_.clear();
    }
    elements_.push_back(std::move(value));
}

bool Value::Parse(const std::string& text, Value& out, std::string* error) {
    Parser parser(text);
    Value result;
    if (!parser.Parse(result)) {
        if (error != nullptr) {
            *error = parser.error();
        }
        return false;
    }
    out = std::move(result);
    return true;
}

std::string Value::Dump(int indent) const {
    std::string out;
    DumpTo(out, indent, 0);
    return out;
}

void Value::DumpTo(std::string& out, int indent, int depth) const {
    switch (type_) {
    case Type::Null:
        out += "null";
        break;
    case Type::Bool:
        out += bool_ ? "true" : "false";
        break;
    case Type::Number:
        out += FormatNumber(number_);
        break;
    case Type::String:
        DumpString(out, string_);
        break;
    case Type::Object: {
        if (members_.empty()) {
            out += "{}";
            break;
        }
        out += "{\n";
        for (size_t i = 0; i < members_.size(); ++i) {
            DumpIndent(out, indent, depth + 1);
            DumpString(out, members_[i].first);
            out += ": ";
            members_[i].second.DumpTo(out, indent, depth + 1);
            if (i + 1 < members_.size()) {
                out += ",";
            }
            out += "\n";
        }
        DumpIndent(out, indent, depth);
        out += "}";
        break;
    }
    case Type::Array: {
        if (elements_.empty()) {
            out += "[]";
            break;
        }
        out += "[\n";
        for (size_t i = 0; i < elements_.size(); ++i) {
            DumpIndent(out, indent, depth + 1);
            elements_[i].DumpTo(out, indent, depth + 1);
            if (i + 1 < elements_.size()) {
                out += ",";
            }
            out += "\n";
        }
        DumpIndent(out, indent, depth);
        out += "]";
        break;
    }
    }
}

} // namespace json
} // namespace vb
