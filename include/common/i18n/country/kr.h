/**
 * @file kr.h
 * @brief 대한민국(KR) 국가별 문자열 후보.
 *
 * (언어, 국가) 조합별 override 문자열 후보 모음이다.
 *
 * @note TBD: language/ko.h 대비 일부 상태 메시지가 아직
 * 정의되지 않았다(예: kCorruptedData, kCorruptedWhileParsing).
 * @note TBD: locales.cc 의 by_region 테이블에는 아직
 * 배선(연결)되어 있지 않다.
 */
#pragma once
#include <string_view>

namespace bedrock::locale::country::kr {

namespace status {
/** @brief 성공. */
inline constexpr std::string_view kSuccess = "성공.";
/** @brief 대상이 없음. */
inline constexpr std::string_view kNoEnt = "대상 없음.";
/** @brief 일반 에러. */
inline constexpr std::string_view kError = "에러.";
/** @brief 스트림이 null. */
inline constexpr std::string_view kNullStream = "stream이 null임.";
}  // namespace status

}  // namespace bedrock::locale::country::kr
