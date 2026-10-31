/**
 * @file transcriber.cc
 * @brief TranscriberError의 std::error_category 구현과 Deserializer/
 *        Serializer의 소멸자 정의.
 */
#include "archive/transcriber.h"

#include <format>

#include "common/i18n/locales.h"

namespace bedrock::archive::transcriber {

const std::error_category& TranscriberCategory() noexcept {
  static const struct Cat final : std::error_category {
    [[nodiscard]] const char* name() const noexcept override {
      return "bedrock.archive.transcriber";
    }
    [[nodiscard]] std::string message(int error_value) const override {
      std::string err_msg;
      switch (static_cast<TranscriberError>(error_value)) {
        case TranscriberError::kSnowflakeMissingMachineId:
        case TranscriberError::kSnowflakeInvalidMachineId:
        case TranscriberError::kSnowflakeInvalidStatePath:
        case TranscriberError::kSnowflakeEnvironmentReadFailed:
        case TranscriberError::kSnowflakeConfigurationReadFailed:
        case TranscriberError::kSnowflakeRegistryIoFailed:
        case TranscriberError::kSnowflakeRegistryCorrupt:
        case TranscriberError::kSnowflakeRegistryIncompatible:
        case TranscriberError::kSnowflakeMachineIdMismatch:
        case TranscriberError::kSnowflakeLockFailed:
        case TranscriberError::kSnowflakeSlotsExhausted:
        case TranscriberError::kSnowflakeClockBeforeEpoch:
        case TranscriberError::kSnowflakeClockOutOfRange:
        case TranscriberError::kSnowflakeClockRollback:
        case TranscriberError::kSnowflakeSequenceExhausted:
        case TranscriberError::kSnowflakeUnsupportedPlatform:
          return "Snowflake identity error";
        case TranscriberError::kSuccess: {
          err_msg = GetI18nString(locale::StringKey::kStatusSuccess,
                                  locale::IsO6391::kKO, locale::IsO31661::kKR);
          break;
        }
        case TranscriberError::kNoENT:
          err_msg = GetI18nString(locale::StringKey::kStatusNoEnt,
                                  locale::IsO6391::kKO, locale::IsO31661::kKR);
          break;
        case TranscriberError::kNullStream:
          err_msg = GetI18nString(locale::StringKey::kStatusNullStream,
                                  locale::IsO6391::kKO, locale::IsO31661::kKR);
          break;
        case TranscriberError::kCorrupted:
          err_msg = GetI18nString(locale::StringKey::kStatusCorruptedData,
                                  locale::IsO6391::kKO, locale::IsO31661::kKR);
          break;
        case TranscriberError::kError:
          err_msg = GetI18nString(locale::StringKey::kStatusError,
                                  locale::IsO6391::kKO, locale::IsO31661::kKR);
          break;
      }
      return err_msg.empty() ? "Unknown Error." : err_msg;

    }
  } kInstance;
  return kInstance;
}
std::error_code SnowflakeIdentityError(std::int64_t identity) noexcept {
  if (identity >= 0) return {};
  if (identity < -16) return make_error_code(TranscriberError::kError);
  return {static_cast<int>(0x100 + static_cast<unsigned>(-identity)), TranscriberCategory()};
}
std::error_code make_error_code(TranscriberError error) noexcept {
  return {static_cast<int>(error), TranscriberCategory()};
}

bedrock::Status BorrowStatus(const OwnedStatus& source,
                             std::string& message_storage) {
  message_storage = source.Message();
  const bedrock::ErrorCode code =
      source.Ok() ? bedrock::ErrorCode::kSuccess
                  : static_cast<bedrock::ErrorCode>(source.code.value());
  return bedrock::Status(code, message_storage);
}

OwnedStatus CorruptedAt(std::string_view what) {
  const std::string_view fmt =
      GetI18nString(locale::StringKey::kStatusCorruptedWhileParsing,
                    locale::IsO6391::kKO, locale::IsO31661::kKR);
  return {TranscriberError::kCorrupted,
          std::vformat(fmt, std::make_format_args(what))};
}

ConstructCore::~ConstructCore() = default;
RepresentCore::~RepresentCore() = default;

}  // namespace bedrock::archive::transcriber
