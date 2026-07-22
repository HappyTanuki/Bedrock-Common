/**
 * @file base64.h
 * @brief base64 인코딩/디코딩 유틸리티.
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bedrock::util {

/**
 * @brief RFC 4648 표준 base64(+, /, = 패딩)로 인코딩한다.
 *
 * YAML !!binary 등에 그대로 사용 가능하다. 출력은 한 줄이며,
 * 줄바꿈/들여쓰기는 호출부에서 감싼다.
 * @param data 인코딩할 바이트 데이터.
 * @return base64로 인코딩된 문자열.
 */
std::string Base64Encode(std::span<const std::uint8_t> data);
/**
 * @brief std::byte 버전의 Base64Encode.
 * @param data 인코딩할 바이트 데이터.
 * @return base64로 인코딩된 문자열.
 */
std::string Base64Encode(std::span<const std::byte> data);

/**
 * @brief base64 문자열을 디코드한다.
 *
 * 공백/개행/탭은 무시하므로 YAML 블록에 감싼 base64도 그대로 넘길 수
 * 있다.
 * @param text 디코드할 base64 문자열.
 * @param out 디코드된 바이트가 저장될 벡터. 실패 시 비운다.
 * @return 잘못된 문자·길이면 false, 성공하면 true.
 */
bool Base64Decode(std::string_view text, std::vector<std::uint8_t>& out);
/**
 * @brief std::byte 벡터로 디코드하는 버전.
 * @param text 디코드할 base64 문자열.
 * @param out 디코드된 바이트가 저장될 벡터. 실패 시 비운다.
 * @return 잘못된 문자·길이면 false, 성공하면 true.
 */
bool Base64Decode(std::string_view text, std::vector<std::byte>& out);

}  // namespace bedrock::util
