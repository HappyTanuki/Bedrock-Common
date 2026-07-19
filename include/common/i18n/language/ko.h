#pragma once
#include <string_view>

namespace bedrock::locale::language::ko {

namespace status {
inline constexpr std::string_view kSuccess = "성공.";
inline constexpr std::string_view kNoEnt = "대상이 없습니다.";
inline constexpr std::string_view kError = "에러.";
inline constexpr std::string_view kNullStream = "stream이 null이었습니다.";
inline constexpr std::string_view kCorruptedData =
    "데이터 손상이 감지되었습니다.";
inline constexpr std::string_view kCorruptedWhileParsing =
    "{}를 파싱하는 동안 데이터 손상이 감지되었습니다.";
}  // namespace status

}  // namespace bedrock::locale::language::ko
