#include "Sha256.h"
#include "Text.h"

#ifndef BCRYPT_SHA256_ALGORITHM
#define BCRYPT_SHA256_ALGORITHM L"SHA256"
#endif

namespace sc {

std::wstring HexSha256Utf8(std::wstring_view content) {
  if (content.empty()) return {};                 // 约定：空内容不计算哈希
  const std::string utf8 = ToUtf8(content);
  if (utf8.empty()) return {};

  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE h = nullptr;
  UCHAR digest[32]{};
  std::wstring out;

  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
    if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
      const NTSTATUS s1 = BCryptHashData(
          h, reinterpret_cast<PUCHAR>(const_cast<char*>(utf8.data())),
          static_cast<ULONG>(utf8.size()), 0);
      const NTSTATUS s2 = BCryptFinishHash(h, digest, static_cast<ULONG>(sizeof(digest)), 0);
      BCryptDestroyHash(h);
      if (s1 == 0 && s2 == 0) {
        static constexpr wchar_t kHex[] = L"0123456789abcdef";
        out.reserve(64);
        for (const UCHAR b : digest) {
          out.push_back(kHex[b >> 4]);
          out.push_back(kHex[b & 0xF]);
        }
      }
    }
    BCryptCloseAlgorithmProvider(alg, 0);
  }
  return out;   // 失败返回空串：调用方不额外拒绝，条目照常入列（去重此时按空串比对）
}

}  // namespace sc
