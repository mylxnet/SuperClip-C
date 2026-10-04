#pragma once
#include "../core/Config.h"
#if !defined(_MSC_VER)
#include <initguid.h>       // 必须早于任何 d2d1.h/dwrite.h：在本编译单元内实体化 IID_*
#endif

// 跨工具链取接口 IID：MSVC 用 __uuidof，mingw 用头文件里的 IID_* 常量
#if defined(_MSC_VER)
#define SC_IID(iface) __uuidof(iface)
#else
#define SC_IID(iface) IID_##iface
#endif

// 极简 COM 指针：只用得到 AddRef/Release/QueryInterface 这一档，
// 不引 <wrl/client.h>（MSVC 专属头，交叉编译侧没有）也不引 C++/WinRT 投影。
namespace sc {

template <typename T>
class Com {
 public:
  Com() = default;
  Com(T* p, bool addRef = false) : p_(p) { if (p_ && addRef) p_->AddRef(); }
  Com(const Com& other) : p_(other.p_) { if (p_) p_->AddRef(); }
  Com(Com&& other) noexcept : p_(other.p_) { other.p_ = nullptr; }
  ~Com() { if (p_) p_->Release(); }

  Com& operator=(const Com& other) {
    if (this != &other) {
      if (other.p_) other.p_->AddRef();
      if (p_) p_->Release();
      p_ = other.p_;
    }
    return *this;
  }
  Com& operator=(Com&& other) noexcept {
    if (this != &other) {
      if (p_) p_->Release();
      p_ = other.p_;
      other.p_ = nullptr;
    }
    return *this;
  }
  // 接管一个已有引用计数的裸指针（工厂返回的新对象走这里；put() 亦可）
  Com& attach(T* p) {
    if (p_) p_->Release();
    p_ = p;
    return *this;
  }

  T** put() { if (p_) { p_->Release(); p_ = nullptr; } return &p_; }
  T* get() const { return p_; }
  T* operator->() const { return p_; }
  T& operator*() const { return *p_; }
  explicit operator bool() const { return p_ != nullptr; }
  void reset() { if (p_) p_->Release(); p_ = nullptr; }

 private:
  T* p_ = nullptr;
};

}  // namespace sc
