#include "common/archive/transcriber/transciber.h"
#include "common/i18n/locales.h"

namespace bedrock::archive::transcriber {

const std::error_category& TranscriberCategory() noexcept {
  static const struct Cat final : std::error_category {
    const char* name() const noexcept override {
      return "bedrock.archive.transcriber";
    }
    std::string message(int v) const override {
      std::string err_msg;
      switch (static_cast<TranscriberError>(v)) {
        case TranscriberError::kSuccess: {
          err_msg =
              GetI18nString(locale::StringKey::kStatusSuccess,
                            locale::ISO639_1::kKO, locale::ISO3166_1::kKR);
          break;
        }
        case TranscriberError::kNoENT:
          err_msg =
              GetI18nString(locale::StringKey::kStatusNoEnt,
                            locale::ISO639_1::kKO, locale::ISO3166_1::kKR);
          break;
        case TranscriberError::kNullStream:
          err_msg =
              GetI18nString(locale::StringKey::kStatusNullStream,
                            locale::ISO639_1::kKO, locale::ISO3166_1::kKR);
          break;
        case TranscriberError::kError:
          err_msg =
              GetI18nString(locale::StringKey::kStatusError,
                            locale::ISO639_1::kKO, locale::ISO3166_1::kKR);
          break;
        default:
          return "Unknown Error.";
      }
      return err_msg;
    }
  } instance;
  return instance;
}
std::error_code make_error_code(TranscriberError e) noexcept {
  return {static_cast<int>(e), TranscriberCategory()};
}

Deserializer::~Deserializer() = default;
Serializer::~Serializer() = default;

}  // namespace bedrock::archive::transcriber
