#pragma once
#include <string_view>

namespace bedrock::locale::country::kr {

namespace status {
inline constexpr std::string_view kSuccess = "성공.";
inline constexpr std::string_view kNoEnt = "대상 없음.";
inline constexpr std::string_view kError = "에러.";
inline constexpr std::string_view kNullStream = "stream이 null임.";
}  // namespace status

}  // namespace bedrock::locale::country::kr
