#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bedrock::util {

// RFC 4648 표준 base64(+, /, = 패딩)로 인코딩한다. YAML !!binary 등에 그대로 사용
// 가능. 출력은 한 줄이며, 줄바꿈/들여쓰기는 호출부에서 감싼다.
std::string Base64Encode(std::span<const std::uint8_t> data);
std::string Base64Encode(std::span<const std::byte> data);

// base64 문자열을 디코드한다. 공백/개행/탭은 무시하므로 YAML 블록에 감싼
// base64도 그대로 넘길 수 있다. 잘못된 문자·길이면 false를 반환하고 out은 비운다.
bool Base64Decode(std::string_view text, std::vector<std::uint8_t>& out);
bool Base64Decode(std::string_view text, std::vector<std::byte>& out);

}  // namespace bedrock::util
