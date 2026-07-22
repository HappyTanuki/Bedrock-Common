/**
 * @file status.h
 * @brief 오류 코드(ErrorCode)와 상태(Status) 타입 정의.
 */
#pragma once
#include <cstdint>
#include <string>
#include <system_error>

namespace bedrock {

/**
 * @brief bedrock 공용 오류 코드.
 */
enum class ErrorCode : std::uint32_t {
  /** @brief 성공. */
  kSuccess = 0,
  /** @brief 대상 항목 없음. */
  kNoENT = 1,
  /** @brief 처리되지 않은 일반 오류. */
  kError = 0xFFFFFFFF,
};

/**
 * @brief ErrorCode 전용 std::error_category.
 *
 * 코드로부터 고정 메시지를 그때그때 유도하며 별도로 저장하지 않는다.
 * @return ErrorCode 전용 카테고리의 싱글턴 참조.
 */
const std::error_category& ErrorCategory() noexcept;
/**
 * @brief ErrorCode를 ErrorCategory()와 묶어 std::error_code로 만든다.
 * @param e 오류 코드.
 * @return 생성된 std::error_code.
 */
std::error_code make_error_code(ErrorCode e) noexcept;

/**
 * @brief 오류 코드와 선택적인 동적 상세 메시지를 함께 담는 타입.
 *
 * 미리 정의된 코드는 detail이 비어 있으면 code.message()로 고정
 * 메시지를 사용하고, 동적 오류는 detail을 채워 그때만 소유 문자열(힙)을
 * 사용한다.
 */
struct Status {
  /** @brief 오류 코드. */
  std::error_code code{};
  /** @brief 동적 오류일 때만 사용하는 상세 메시지. */
  std::string detail;

  /** @brief 기본 생성자. */
  Status() = default;
  /**
   * @brief ErrorCode로부터 암시적으로 생성한다.
   * @param e 오류 코드.
   */
  explicit(false) Status(ErrorCode e) : code(make_error_code(e)) {}
  /**
   * @brief ErrorCode와 상세 메시지로 생성한다.
   * @param e 오류 코드.
   * @param detail_ 상세 메시지.
   */
  Status(ErrorCode e, std::string detail_)
      : code(make_error_code(e)), detail(std::move(detail_)) {}

  /**
   * @brief 다른 서브시스템의 error_code도 그대로 담을 수 있다.
   * @param code_ 표준 error_code.
   */
  explicit(false) Status(std::error_code code_) : code(code_) {}
  /**
   * @brief error_code와 상세 메시지로 생성한다.
   * @param code_ 표준 error_code.
   * @param detail_ 상세 메시지.
   */
  Status(std::error_code code_, std::string detail_)
      : code(code_), detail(std::move(detail_)) {}

  /**
   * @brief int도 그대로 담을 수 있다.
   * @param e ErrorCode로 캐스팅될 정수 값.
   */
  explicit(false) Status(int e)
      : code(make_error_code(static_cast<ErrorCode>(e))) {}
  /**
   * @brief 정수 코드와 상세 메시지로 생성한다.
   * @param e ErrorCode로 캐스팅될 정수 값.
   * @param detail_ 상세 메시지.
   */
  Status(int e, std::string detail_)
      : code(make_error_code(static_cast<ErrorCode>(e))),
        detail(std::move(detail_)) {}

  /** @brief 성공 여부를 반환한다. */
  constexpr bool ok() const noexcept { return !code; }
  /** @brief 실패 여부를 반환한다. */
  constexpr bool failed() const noexcept { return static_cast<bool>(code); }

  /**
   * @brief 오류 메시지를 반환한다.
   * @return detail이 있으면 detail, 없으면 code.message().
   */
  std::string message() const {
    return detail.empty() ? code.message() : detail;
  }
};

}  // namespace bedrock

/**
 * @brief ErrorCode를 std::error_code의 소스로 등록한다.
 */
template <>
struct std::is_error_code_enum<bedrock::ErrorCode> : std::true_type {};
