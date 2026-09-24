#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace vb {
namespace json {

// 轻量 JSON 读写实现（对象保持插入顺序），用于配置文件解析与生成
class Value {
public:
    enum class Type { Null, Bool, Number, String, Object, Array };

    Value() = default;

    explicit Value(bool value) : type_(Type::Bool), bool_(value) {}
    explicit Value(double value) : type_(Type::Number), number_(value) {}
    explicit Value(int value) : type_(Type::Number), number_(static_cast<double>(value)) {}
    explicit Value(std::string value) : type_(Type::String), string_(std::move(value)) {}
    explicit Value(const char* value)
        : type_(Type::String), string_(value != nullptr ? value : "") {}

    static Value MakeObject();
    static Value MakeArray();

    Type type() const { return type_; }
    bool IsNull() const { return type_ == Type::Null; }
    bool IsBool() const { return type_ == Type::Bool; }
    bool IsNumber() const { return type_ == Type::Number; }
    bool IsString() const { return type_ == Type::String; }
    bool IsObject() const { return type_ == Type::Object; }
    bool IsArray() const { return type_ == Type::Array; }

    bool AsBool(bool fallback) const;
    double AsNumber(double fallback) const;
    int AsInt(int fallback) const;
    std::string AsString(const std::string& fallback) const;

    // 对象访问：不存在时返回 nullptr
    const Value* Find(const std::string& key) const;
    Value& Set(const std::string& key, Value value);
    const std::vector<std::pair<std::string, Value>>& Members() const { return members_; }

    // 数组访问
    size_t Size() const;
    const Value& At(size_t index) const;
    void Push(Value value);

    static bool Parse(const std::string& text, Value& out, std::string* error = nullptr);
    std::string Dump(int indent = 2) const;

private:
    void DumpTo(std::string& out, int indent, int depth) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<std::pair<std::string, Value>> members_;
    std::vector<Value> elements_;
};

} // namespace json
} // namespace vb
