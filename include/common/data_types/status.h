#pragma once
#include <cstdint>
#include <string>
#include <system_error>

namespace bedrock {

enum class ErrorCode : std::uint32_t {
  kSuccess = 0,
  kNoENT = 1,
  kError = 0xFFFFFFFF,
};

// ErrorCode 전용 카테고리. 코드 -> 고정 메시지(유도, 저장 안 함).
const std::error_category& ErrorCategory() noexcept;
std::error_code make_error_code(ErrorCode e) noexcept;

// 코드 + 선택적 동적 상세 메시지.
//   - 미리 정의된 코드: detail 비움 -> code.message()로 고정 메시지.
//   - 동적 오류: detail 채움 -> 그때만 소유 문자열(힙) 사용.
struct Status {
  std::error_code code{};
  std::string detail;

  Status() = default;
  explicit(false) Status(ErrorCode e) : code(make_error_code(e)) {}
  Status(ErrorCode e, std::string detail_)
      : code(make_error_code(e)), detail(std::move(detail_)) {}

  // 다른 서브시스템의 error_code도 그대로 담을 수 있다.
  explicit(false) Status(std::error_code code_) : code(code_) {}
  Status(std::error_code code_, std::string detail_)
      : code(code_), detail(std::move(detail_)) {}

  // int도 그대로 담을 수 있다.
  explicit(false) Status(int e)
      : code(make_error_code(static_cast<ErrorCode>(e))) {}
  Status(int e, std::string detail_)
      : code(make_error_code(static_cast<ErrorCode>(e))),
        detail(std::move(detail_)) {}

  constexpr bool ok() const noexcept { return !code; }
  constexpr bool failed() const noexcept { return static_cast<bool>(code); }

  std::string message() const {
    return detail.empty() ? code.message() : detail;
  }
};

}  // namespace bedrock

// ErrorCode를 std::error_code 소스로 등록
template <>
struct std::is_error_code_enum<bedrock::ErrorCode> : std::true_type {};
