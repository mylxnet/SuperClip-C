#include "Json.h"
#include "Text.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sc {

const JsonValue JsonValue::kNull{};

const JsonValue& JsonValue::at(std::wstring_view key) const {
  for (const auto& [k, v] : obj_) {
    if (k == key) return v;
  }
  return kNull;
}

bool JsonValue::has(std::wstring_view key) const {
  for (const auto& [k, v] : obj_) {
    if (k == key) return true;
  }
  return false;
}

void JsonValue::set(std::wstring key, JsonValue v) {
  kind_ = Kind::Object;
  for (auto& [k, existing] : obj_) {
    if (k == key) { existing = std::move(v); return; }
  }
  obj_.emplace_back(std::move(key), std::move(v));
}

void JsonValue::push(JsonValue v) {
  kind_ = Kind::Array;
  arr_.push_back(std::move(v));
}

namespace {

bool Hex4(const char* p, unsigned& out) {
  out = 0;
  for (int i = 0; i < 4; ++i) {
    const char c = p[i];
    unsigned d;
    if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
    else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
    else return false;
    out = out * 16 + d;
  }
  return true;
}

struct Reader {
  const char* p;
  const char* end;
  std::wstring& err;

  bool Eof() const { return p >= end; }
  char Peek() const { return p < end ? *p : '\0'; }

  void SkipWs() {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
  }

  bool Fail(const wchar_t* msg) {
    err = msg;
    return false;
  }

  bool ParseString(std::wstring& out) {
    if (Peek() != '"') return Fail(L"期望字符串");
    ++p;
    out.clear();
    std::string pending;
    auto flush = [&pending, &out]() {
      if (!pending.empty()) {
        out += FromUtf8(pending);
        pending.clear();
      }
    };
    while (true) {
      if (Eof()) return Fail(L"字符串未闭合");
      const char c = *p++;
      if (c == '"') { flush(); return true; }
      if (c != '\\') { pending.push_back(c); continue; }
      if (Eof()) return Fail(L"转义未闭合");
      const char e = *p++;
      wchar_t ch = 0;
      switch (e) {
        case '"': ch = L'"'; break;
        case '\\': ch = L'\\'; break;
        case '/': ch = L'/'; break;
        case 'b': ch = L'\b'; break;
        case 'f': ch = L'\f'; break;
        case 'n': ch = L'\n'; break;
        case 'r': ch = L'\r'; break;
        case 't': ch = L'\t'; break;
        case 'u': {
          unsigned hi = 0;
          if (p + 4 > end || !Hex4(p, hi)) return Fail(L"\\u 非法");
          p += 4;
          flush();
          if (hi >= 0xD800 && hi <= 0xDBFF) {
            if (p + 6 <= end && p[0] == '\\' && p[1] == 'u') {
              unsigned lo = 0;
              if (Hex4(p + 2, lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                out.push_back(static_cast<wchar_t>(hi));
                out.push_back(static_cast<wchar_t>(lo));
                p += 6;
                break;
              }
            }
            out.push_back(static_cast<wchar_t>(hi));   // 落单高位代理原样保留
            break;
          }
          out.push_back(static_cast<wchar_t>(hi));
          break;
        }
        default:
          return Fail(L"未知转义");
      }
      if (e != 'u') {
        flush();
        out.push_back(ch);
      }
    }
  }

  bool ParseNumber(double& out) {
    const char* start = p;
    if (Peek() == '-' || Peek() == '+') ++p;
    while (!Eof()) {
      const char c = *p;
      if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') ++p;
      else break;
    }
    if (p == start) return Fail(L"数字非法");
    const std::string token(start, static_cast<size_t>(p - start));
    char* stop = nullptr;
    out = std::strtod(token.c_str(), &stop);
    if (stop == token.c_str()) return Fail(L"数字解析失败");
    return true;
  }

  bool ParseValue(JsonValue& out, int depth) {
    if (depth > 8) return Fail(L"嵌套过深");
    SkipWs();
    const char c = Peek();
    if (c == '"') {
      std::wstring s;
      if (!ParseString(s)) return false;
      out = JsonValue::MakeString(std::move(s));
      return true;
    }
    if (c == '{') {
      ++p;
      JsonValue obj = JsonValue::MakeObject();
      SkipWs();
      if (Peek() == '}') { ++p; out = std::move(obj); return true; }
      while (true) {
        SkipWs();
        std::wstring key;
        if (!ParseString(key)) return false;
        SkipWs();
        if (Peek() != ':') return Fail(L"期望 ':'");
        ++p;
        JsonValue val;
        if (!ParseValue(val, depth + 1)) return false;
        obj.set(std::move(key), std::move(val));
        SkipWs();
        if (Peek() == ',') { ++p; continue; }
        if (Peek() == '}') { ++p; break; }
        return Fail(L"期望 ',' 或 '}'");
      }
      out = std::move(obj);
      return true;
    }
    if (c == '[') {
      ++p;
      JsonValue arr = JsonValue::MakeArray();
      SkipWs();
      if (Peek() == ']') { ++p; out = std::move(arr); return true; }
      while (true) {
        JsonValue val;
        if (!ParseValue(val, depth + 1)) return false;
        arr.push(std::move(val));
        SkipWs();
        if (Peek() == ',') { ++p; continue; }
        if (Peek() == ']') { ++p; break; }
        return Fail(L"期望 ',' 或 ']'");
      }
      out = std::move(arr);
      return true;
    }
    if (end - p >= 4 && std::memcmp(p, "true", 4) == 0) { p += 4; out = JsonValue::MakeBool(true); return true; }
    if (end - p >= 5 && std::memcmp(p, "false", 5) == 0) { p += 5; out = JsonValue::MakeBool(false); return true; }
    if (end - p >= 4 && std::memcmp(p, "null", 4) == 0) { p += 4; out = JsonValue{}; return true; }
    double d = 0;
    if (!ParseNumber(d)) return false;
    out = JsonValue::MakeNumber(d);
    return true;
  }
};

void WriteString(const std::wstring& s, std::string& out) {
  out.push_back('"');
  const std::string utf8 = ToUtf8(s);
  for (const char c : utf8) {
    const unsigned char u = static_cast<unsigned char>(c);
    switch (c) {
      case '"': out.append("\\\""); break;
      case '\\': out.append("\\\\"); break;
      case '\n': out.append("\\n"); break;
      case '\r': out.append("\\r"); break;
      case '\t': out.append("\\t"); break;
      default:
        if (u < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", u);
          out.append(buf);
        } else {
          out.push_back(c);            // 非 ASCII 原样输出（明文 UTF-8）
        }
    }
  }
  out.push_back('"');
}

void WriteValue(const JsonValue& v, std::string& out) {
  switch (v.kind()) {
    case JsonValue::Kind::Null: out.append("null"); break;
    case JsonValue::Kind::Bool: out.append(v.asBool() ? "true" : "false"); break;
    case JsonValue::Kind::Number: {
      const double d = v.asNumber();
      const long long ll = static_cast<long long>(d);
      if (static_cast<double>(ll) == d) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", ll);
        out.append(buf);
      } else {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%.10g", d);
        out.append(buf);
      }
      break;
    }
    case JsonValue::Kind::String: WriteString(v.asString(), out); break;
    case JsonValue::Kind::Array: {
      out.push_back('[');
      bool first = true;
      for (const auto& item : v.items()) {
        if (!first) out.push_back(',');
        first = false;
        WriteValue(item, out);
      }
      out.push_back(']');
      break;
    }
    case JsonValue::Kind::Object: {
      out.push_back('{');
      bool first = true;
      for (const auto& [k, val] : v.members()) {
        if (!first) out.push_back(',');
        first = false;
        WriteString(k, out);
        out.push_back(':');
        WriteValue(val, out);
      }
      out.push_back('}');
      break;
    }
  }
}

}  // namespace

bool JsonParse(std::string_view utf8, JsonValue& out, std::wstring& err) {
  err.clear();
  if (utf8.empty()) {
    out = JsonValue::MakeArray();   // 空文件按空列表处理（§8.4 容错）
    return true;
  }
  Reader r{utf8.data(), utf8.data() + utf8.size(), err};
  if (!r.ParseValue(out, 0)) return false;
  r.SkipWs();
  if (!r.Eof()) {
    err = L"尾随内容";
    return false;
  }
  return true;
}

std::string JsonWrite(const JsonValue& v) {
  std::string out;
  WriteValue(v, out);
  return out;
}

}  // namespace sc
