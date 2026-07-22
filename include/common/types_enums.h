/**
 * @file types_enums.h
 * @brief 상태(Status) 관련 concept과 공용 템플릿 타입 정의.
 */
#pragma once
#include <type_traits>

namespace bedrock {

/**
 * @brief kFailure, kSuccess 열거자를 가진 enum 타입(Enum)을 요구하는 concept.
 * @note clang의 -Wdocumentation이 concept에 붙는 tparam 태그를 인정하지
 *       않아 템플릿 인자 설명은 본문으로 적는다.
 */
template <typename Enum>
concept StatusEnum = std::is_enum_v<Enum> && requires {
  Enum::kFailure;
  Enum::kSuccess;
};

/**
 * @brief 데이터와 그에 대한 상태 값을 함께 담는 타입.
 * @tparam T 데이터 타입.
 * @tparam StatusType StatusEnum을 만족하는 상태 열거형 타입.
 */
template <typename T, typename StatusType>
  requires StatusEnum<StatusType>
struct DataWithStatus {
  /** @brief 데이터. */
  T data;
  /** @brief 데이터에 대한 상태 값. */
  StatusType status;
};

}  // namespace bedrock
