/**
 * @file unicode.cc
 * @brief UTF-8 ↔ UTF-32 변환 유틸리티 구현.
 */
#include "common/util/unicode.h"

namespace bedrock::util {

void AppendUtf8(std::string& out, char32_t cp) {
  const std::uint32_t c = static_cast<std::uint32_t>(cp);
  if (c <= 0x7F) {
    out += static_cast<char>(c);
  } else if (c <= 0x7FF) {
    out += static_cast<char>(0xC0 | (c >> 6));
    out += static_cast<char>(0x80 | (c & 0x3F));
  } else if (c <= 0xFFFF) {
    out += static_cast<char>(0xE0 | (c >> 12));
    out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (c & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (c >> 18));
    out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (c & 0x3F));
  }
}

std::string EncodeUtf8(std::span<const std::uint32_t> cps) {
  std::string out;
  out.reserve(cps.size());
  for (const std::uint32_t c : cps) {
    AppendUtf8(out, static_cast<char32_t>(c));
  }
  return out;
}

bool DecodeUtf8Seq(std::string_view in, std::size_t i, char32_t& cp,
                   std::size_t& len) {
  const unsigned char c = static_cast<unsigned char>(in[i]);
  std::uint32_t v = 0;
  if ((c & 0xE0) == 0xC0) {
    len = 2;
    v = c & 0x1FU;
  } else if ((c & 0xF0) == 0xE0) {
    len = 3;
    v = c & 0x0FU;
  } else if ((c & 0xF8) == 0xF0) {
    len = 4;
    v = c & 0x07U;
  } else {
    return false;  // 잘못된 리드 바이트
  }
  if (i + len > in.size()) {
    return false;  // 잘린 시퀀스
  }
  for (std::size_t k = 1; k < len; ++k) {
    const unsigned char b = static_cast<unsigned char>(in[i + k]);
    if ((b & 0xC0) != 0x80) {
      return false;  // 연속 바이트 아님
    }
    v = (v << 6) | (b & 0x3FU);
  }
  cp = static_cast<char32_t>(v);
  return true;
}

bool DecodeUtf8(std::string_view in, std::vector<std::uint32_t>& out) {
  out.clear();
  out.reserve(in.size());
  for (std::size_t i = 0; i < in.size();) {
    const unsigned char c = static_cast<unsigned char>(in[i]);
    if (c < 0x80) {
      out.push_back(c);
      i++;
      continue;
    }
    char32_t cp;
    std::size_t len;
    if (!DecodeUtf8Seq(in, i, cp, len)) {
      return false;
    }
    const std::uint32_t v = static_cast<std::uint32_t>(cp);
    if (v > 0x10FFFF || (0xD800 <= v && v <= 0xDFFF)) {
      return false;  // 범위 밖/서로게이트
    }
    out.push_back(v);
    i += len;
  }
  return true;
}

}  // namespace bedrock::util
