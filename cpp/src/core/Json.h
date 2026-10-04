#pragma once
#include "Config.h"
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// 极简 JSON（本项目只需「对象数组」这一种形态）。
// 决策记录：原 ADR 选 RapidJSON，但当前环境离线无法取包 → 内置 180 行实现。
// 读侧必须同时吃 "\uXXXX" 转义（.NET System.Text.Json 默认输出）与明文 UTF-8；
// 写侧统一输出明文 UTF-8、不转义非 ASCII。
namespace sc {

class JsonValue {
 public:
  enum class Kind { Null, Bool, Number, String, Array, Object };

  JsonValue() = default;
  static JsonValue MakeObject() { JsonValue v; v.kind_ = Kind::Object; return v; }
  static JsonValue MakeArray() { JsonValue v; v.kind_ = Kind::Array; return v; }
  static JsonValue MakeString(std::wstring s) { JsonValue v; v.kind_ = Kind::String; v.str_ = std::move(s); return v; }
  static JsonValue MakeNumber(double n) { JsonValue v; v.kind_ = Kind::Number; v.num_ = n; return v; }
  static JsonValue MakeBool(bool b) { JsonValue v; v.kind_ = Kind::Bool; v.bool_ = b; return v; }

  Kind kind() const { return kind_; }
  bool isNull() const { return kind_ == Kind::Null; }
  bool isObject() const { return kind_ == Kind::Object; }
  bool isArray() const { return kind_ == Kind::Array; }
  bool asBool(bool fallback = false) const { return kind_ == Kind::Bool ? bool_ : (kind_ == Kind::Number ? num_ != 0 : fallback); }
  double asNumber(double fallback = 0) const {
    if (kind_ == Kind::Number) return num_;
    if (kind_ == Kind::Bool) return bool_ ? 1 : 0;
    return fallback;
  }
  const std::wstring& asString() const { return str_; }

  // 对象成员访问；不存在返回 Null 值
  const JsonValue& at(std::wstring_view key) const;
  bool has(std::wstring_view key) const;
  void set(std::wstring key, JsonValue v);
  void push(JsonValue v);

  const std::vector<JsonValue>& items() const { return arr_; }
  const std::vector<std::pair<std::wstring, JsonValue>>& members() const { return obj_; }

 private:
  Kind kind_ = Kind::Null;
  bool bool_ = false;
  double num_ = 0;
  std::wstring str_;
  std::vector<JsonValue> arr_;
  std::vector<std::pair<std::wstring, JsonValue>> obj_;
  static const JsonValue kNull;
};

// 解析失败返回 false 并给出简short原因（用于日志）；out 仅在 true 时有效。
bool JsonParse(std::string_view utf8, JsonValue& out, std::wstring& err);

// 紧凑单行输出（无缩进），UTF-8。
std::string JsonWrite(const JsonValue& v);

}  // namespace sc
