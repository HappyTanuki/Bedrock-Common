/**
 * @file unicode.h
 * @brief UTF-8 ↔ UTF-32 변환 유틸리티(포맷 독립).
 *
 * 아카이브 포맷 계층(yaml 등)이 공유하는 코덱이다: 파서는 입력을
 * 코드포인트 열로 디코드해 다루고, 값 문자열은 UTF-8로 보관한다.
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bedrock::util {

/** @brief 코드포인트 하나를 UTF-8로 인코드해 out 끝에 붙인다. */
void AppendUtf8(std::string& out, char32_t cp);

/** @brief 코드포인트 열을 UTF-8 문자열로 인코드한다. */
std::string EncodeUtf8(std::span<const std::uint32_t> cps);

/**
 * @brief UTF-8 바이트열 전체를 코드포인트 열로 디코드한다.
 * @param in UTF-8 입력.
 * @param out 디코드 결과(성공 시 채워짐).
 * @return 잘못된 시퀀스(리드/연속 바이트, 범위 밖, 서로게이트)면 false.
 */
bool DecodeUtf8(std::string_view in, std::vector<std::uint32_t>& out);

/**
 * @brief UTF-8 멀티바이트 시퀀스(2~4바이트) 하나를 디코드한다.
 * @param in 원본 바이트열.
 * @param i 디코드를 시작할 위치(리드 바이트 인덱스).
 * @param cp 성공 시 디코드된 코드포인트가 채워진다.
 * @param len 성공 시 시퀀스 길이(바이트 수)가 채워진다.
 * @return 성공하면 true. 잘못된 리드 바이트나 잘린 시퀀스면 false.
 */
bool DecodeUtf8Seq(std::string_view in, std::size_t i, char32_t& cp,
                   std::size_t& len);

/**
 * @brief 입력 바이트열을 UTF-8 코드포인트 단위로 순회한다.
 *
 * ASCII(1바이트)와 멀티바이트 분기·디코드를 한곳에 모아, 호출자가
 * 순회 루프를 중복 구현하지 않게 한다.
 * @param in 순회할 원본 바이트열.
 * @param emit 코드포인트마다 호출되는 emit(cp, bytes, len, valid):
 *  - valid=true : 정상 코드포인트(bytes/len은 원본 UTF-8 바이트).
 *  - valid=false: 깨진/잘린 시퀀스(bytes[0] 한 바이트, cp는 무의미).
 */
template <typename F>
void ForEachUtf8Char(std::string_view in, F&& emit) {
  for (std::size_t i = 0; i < in.size();) {
    const unsigned char c = static_cast<unsigned char>(in[i]);
    if (c < 0x80) {
      emit(static_cast<char32_t>(c), in.data() + i, std::size_t{1}, true);
      ++i;
    } else {
      char32_t cp;
      std::size_t len;
      if (DecodeUtf8Seq(in, i, cp, len)) {
        emit(cp, in.data() + i, len, true);
        i += len;
      } else {
        emit(char32_t{0}, in.data() + i, std::size_t{1}, false);
        ++i;
      }
    }
  }
}

}  // namespace bedrock::util
