/**
 * @file base64.cc
 * @brief base64 인코딩/디코딩 구현.
 */
#include "common/util/base64.h"

#include <string_view>

namespace bedrock::util {

namespace {

constexpr std::string_view kEncTable =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * @brief base64 문자를 6bit 값으로 변환한다.
 * @param c base64 문자.
 * @return 6bit 값(0~63), 유효하지 않으면 -1.
 */
int DecodeChar(unsigned char character) {
  if (character >= 'A' && character <= 'Z') {
    return character - 'A';
  }
  if (character >= 'a' && character <= 'z') {
    return character - 'a' + 26;
  }
  if (character >= '0' && character <= '9') {
    return character - '0' + 52;
  }
  if (character == '+') {
    return 62;
  }
  if (character == '/') {
    return 63;
  }
  return -1;
}

/**
 * @brief base64 디코딩 시 무시할 공백류 문자인지 확인한다.
 * @param c 검사할 문자.
 * @return 공백/개행/탭 등이면 true.
 */
bool IsSkippable(unsigned char character) {
  return character == ' ' || character == '\n' || character == '\r' ||
         character == '\t' || character == '\f' || character == '\v';
}

}  // namespace

std::string Base64Encode(std::span<const std::uint8_t> data) {
  std::string out;
  out.reserve(((data.size() + 2) / 3) * 4);

  std::size_t index = 0;
  for (; index + 3 <= data.size(); index += 3) {
    std::uint32_t encoded_bits = (std::uint32_t{data[index]} << 16) |
                                 (std::uint32_t{data[index + 1]} << 8) |
                                 std::uint32_t{data[index + 2]};
    out += kEncTable[(encoded_bits >> 18) & 0x3F];
    out += kEncTable[(encoded_bits >> 12) & 0x3F];
    out += kEncTable[(encoded_bits >> 6) & 0x3F];
    out += kEncTable[encoded_bits & 0x3F];
  }

  // 남은 1~2바이트 처리 + = 패딩
  const std::size_t rem = data.size() - index;
  if (rem == 1) {
    std::uint32_t encoded_bits = std::uint32_t{data[index]} << 16;
    out += kEncTable[(encoded_bits >> 18) & 0x3F];
    out += kEncTable[(encoded_bits >> 12) & 0x3F];
    out += '=';
    out += '=';
  } else if (rem == 2) {
    std::uint32_t encoded_bits = (std::uint32_t{data[index]} << 16) |
                                 (std::uint32_t{data[index + 1]} << 8);
    out += kEncTable[(encoded_bits >> 18) & 0x3F];
    out += kEncTable[(encoded_bits >> 12) & 0x3F];
    out += kEncTable[(encoded_bits >> 6) & 0x3F];
    out += '=';
  }
  return out;
}

std::string Base64Encode(std::span<const std::byte> data) {
  return Base64Encode(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(data.data()), data.size()));
}

bool Base64Decode(std::string_view text, std::vector<std::uint8_t>& out) {
  out.clear();

  std::uint32_t buf = 0;
  int nbits = 0;
  std::size_t ndata = 0;  // 패딩을 제외한 유효 데이터 문자 수
  bool padded = false;

  for (unsigned char character : text) {
    if (IsSkippable(character)) {
      continue;
    }
    if (character == '=') {
      padded = true;
      continue;
    }
    if (padded) {
      return false;  // 패딩 뒤에 데이터가 오면 잘못된 형식
    }
    int decoded_value = DecodeChar(character);
    if (decoded_value < 0) {
      out.clear();
      return false;  // 잘못된 문자
    }
    buf = (buf << 6) | static_cast<std::uint32_t>(decoded_value);
    nbits += 6;
    ++ndata;
    if (nbits >= 8) {
      nbits -= 8;
      out.push_back(static_cast<std::uint8_t>((buf >> nbits) & 0xFF));
    }
  }

  if (ndata % 4 == 1) {
    out.clear();
    return false;  // 4로 나눈 나머지 1은 불가능한 길이
  }
  // 마지막 그룹의 남은 비트(패딩 자리)는 0이어야 한다.
  if (nbits > 0 && (buf & ((1U << nbits) - 1)) != 0) {
    out.clear();
    return false;
  }
  return true;
}

bool Base64Decode(std::string_view text, std::vector<std::byte>& out) {
  std::vector<std::uint8_t> tmp;
  if (!Base64Decode(text, tmp)) {
    out.clear();
    return false;
  }
  out.assign(reinterpret_cast<const std::byte*>(tmp.data()),
             reinterpret_cast<const std::byte*>(tmp.data()) + tmp.size());
  return true;
}

}  // namespace bedrock::util
