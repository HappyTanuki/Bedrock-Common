/**
 * @file transcriber.cc
 * @brief TranscriberError의 std::error_category 구현과 Deserializer/
 *        Serializer의 소멸자 정의.
 */
#include "archive/transcriber.h"

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
        case TranscriberError::kError:
          err_msg = GetI18nString(locale::StringKey::kStatusError,
                                  locale::IsO6391::kKO, locale::IsO31661::kKR);
          break;
        default:
          return "Unknown Error.";
      }
      return err_msg;
    }
  } kInstance;
  return kInstance;
}
std::error_code make_error_code(TranscriberError error) noexcept {
  return {static_cast<int>(error), TranscriberCategory()};
}

ConstructCore::~ConstructCore() = default;
RepresentCore::~RepresentCore() = default;

}  // namespace bedrock::archive::transcriber
