/**
 * @file ko.h
 * @brief 한국어(ko) 언어 기본 문자열 테이블.
 *
 * locales.cc 의 번역 테이블에서 ISO639_1::kKO 언어에 대한 기본 문자열로
 * 쓰인다.
 */
#pragma once
#include <string_view>



namespace bedrock::locale::language::ko::status {
/** @brief 성공. */
inline constexpr std::string_view kSuccess = "성공.";
/** @brief 대상이 없음. */
inline constexpr std::string_view kNoEnt = "대상이 없습니다.";
/** @brief 일반 에러. */
inline constexpr std::string_view kError = "에러.";
/** @brief 스트림이 null. */
inline constexpr std::string_view kNullStream = "stream이 null이었습니다.";
/** @brief 데이터 손상이 감지됨. */
inline constexpr std::string_view kCorruptedData =
    "데이터 손상이 감지되었습니다.";
/**
 * @brief 파싱 도중 데이터 손상이 감지됨.
 * `{}` 는 파싱 대상 이름이 들어갈 포맷 자리표시자.
 */
inline constexpr std::string_view kCorruptedWhileParsing =
    "{}를 파싱하는 동안 데이터 손상이 감지되었습니다.";
} // namespace bedrock::locale::language::ko::status


