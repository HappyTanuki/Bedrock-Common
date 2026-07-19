#pragma once
#include <type_traits>

namespace bedrock {

template <typename Enum>
concept StatusEnum = std::is_enum_v<Enum> && requires {
  Enum::kFailure;
  Enum::kSuccess;
};

template <typename T, typename StatusType>
  requires StatusEnum<StatusType>
struct DataWithStatus {
  T data;
  StatusType status;
};

}  // namespace bedrock
