/**
 * @file locales.h
 * @brief i18n(다국어) 상태 문자열 조회 API 선언.
 *
 * StringKey 로 지정한 문자열을 언어(ISO639_1)/국가(ISO3166_1) 조합에 맞게
 * 골라주는 GetI18nString() 을 제공한다. 실제 번역 테이블은
 * src/common/i18n/locales.cc 에 정의되어 있다.
 */
#pragma once

#include <cstdint>
#include <string_view>

/**
 * @brief ISO3166_1(국가), ISO639_1(언어) enum.
 * tools/gen_locales.py 로 생성된다.
 */
#include "common/i18n/iso_codes.gen.h"

namespace bedrock::locale {

/**
 * @brief i18n 문자열 카탈로그의 키.
 *
 * GetI18nString() 호출 시 원하는 문자열을 이 값으로 지정한다.
 */
enum class StringKey : std::uint8_t {
  /** @brief 성공. */
  kStatusSuccess,
  /** @brief 대상이 없음(엔트리 없음). */
  kStatusNoEnt,
  /** @brief 일반 에러. */
  kStatusError,
  /** @brief 스트림이 null. */
  kStatusNullStream,
  /** @brief 데이터 손상이 감지됨. */
  kStatusCorruptedData,
  /**
   * @brief 파싱 도중 데이터 손상이 감지됨.
   * 파싱 대상 이름이 들어갈 포맷 자리표시자를 포함한다.
   */
  kStatusCorruptedWhileParsing
};

/**
 * @brief 키와 언어/국가 조합에 해당하는 현지화 문자열을 조회한다.
 *
 * (language, country) 지역별 override, language 언어별 기본값, 폴백 언어
 * 기본값 순서로 검색하며, 어디에도 없으면 빈 string_view 를 반환한다.
 *
 * @param key i18n 문자열을 식별하는 키.
 * @param language 요청 언어(ISO639_1).
 * @param country 요청 국가(ISO3166_1).
 * @return 조회된 문자열. 없으면 빈 string_view.
 * @note TBD: 현재 테이블 데이터는 language::ko 뿐이라, 한국어(kKO)가
 * 아니면 폴백을 포함해 조회에 실패해 빈 문자열이 반환된다.
 */
std::string_view GetI18nString(const StringKey& key, const IsO6391& language,
                               const IsO31661& country);

}  // namespace bedrock::locale
