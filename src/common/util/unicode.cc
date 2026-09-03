/**
 * @file unicode.cc
 * @brief UTF-8 <-> UTF-32 변환 유틸리티 구현.
 */
#include "common/util/unicode.h"

namespace bedrock::util {

void AppendUtf8(std::string& out, char32_t code_point) {
  const auto code_unit = static_cast<std::uint32_t>(code_point);
  if (code_unit <= 0x7F) {
    out += static_cast<char>(code_unit);
  } else if (code_unit <= 0x7FF) {
    out += static_cast<char>(0xC0 | (code_unit >> 6));
    out += static_cast<char>(0x80 | (code_unit & 0x3F));
  } else if (code_unit <= 0xFFFF) {
    out += static_cast<char>(0xE0 | (code_unit >> 12));
    out += static_cast<char>(0x80 | ((code_unit >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (code_unit & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (code_unit >> 18));
    out += static_cast<char>(0x80 | ((code_unit >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((code_unit >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (code_unit & 0x3F));
  }
}

std::string EncodeUtf8(std::span<const std::uint32_t> code_points) {
  std::string out;
  out.reserve(code_points.size());
  for (const std::uint32_t code_unit : code_points) {
    AppendUtf8(out, static_cast<char32_t>(code_unit));
  }
  return out;
}

bool DecodeUtf8Seq(std::string_view input, std::size_t byte_index,
                   char32_t& code_point, std::size_t& sequence_length) {
  const auto code_unit = static_cast<unsigned char>(input[byte_index]);
  std::uint32_t decoded_value = 0;
  if ((code_unit & 0xE0) == 0xC0) {
    sequence_length = 2;
    decoded_value = code_unit & 0x1FU;
  } else if ((code_unit & 0xF0) == 0xE0) {
    sequence_length = 3;
    decoded_value = code_unit & 0x0FU;
  } else if ((code_unit & 0xF8) == 0xF0) {
    sequence_length = 4;
    decoded_value = code_unit & 0x07U;
  } else {
    return false;  // 잘못된 리드 바이트
  }
  if (byte_index + sequence_length > input.size()) {
    return false;  // 잘린 시퀀스
  }
  for (std::size_t continuation_index = 1; continuation_index < sequence_length;
       ++continuation_index) {
    const auto continuation_byte =
        static_cast<unsigned char>(input[byte_index + continuation_index]);
    if ((continuation_byte & 0xC0) != 0x80) {
      return false;  // 연속 바이트 아님
    }
    decoded_value = (decoded_value << 6) | (continuation_byte & 0x3FU);
  }
  code_point = static_cast<char32_t>(decoded_value);
  return true;
}

bool DecodeUtf8(std::string_view input, std::vector<std::uint32_t>& out) {
  out.clear();
  out.reserve(input.size());
  for (std::size_t byte_index = 0; byte_index < input.size();) {
    const auto code_unit = static_cast<unsigned char>(input[byte_index]);
    if (code_unit < 0x80) {
      out.push_back(code_unit);
      byte_index++;
      continue;
    }
    char32_t code_point = 0;
    std::size_t sequence_length = 0;
    if (!DecodeUtf8Seq(input, byte_index, code_point, sequence_length)) {
      return false;
    }
    const auto decoded_value = static_cast<std::uint32_t>(code_point);
    if (decoded_value > 0x10FFFF ||
        (0xD800 <= decoded_value && decoded_value <= 0xDFFF)) {
      return false;  // 범위 밖/서로게이트
    }
    out.push_back(decoded_value);
    byte_index += sequence_length;
  }
  return true;
}

}  // namespace bedrock::util
