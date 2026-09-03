/**
 * @file yaml_deserializer.cc
 * @brief YAMLDeserializer public API와 private Impl을 §3.1 Load에 연결한다.
 */
#include "common/archive/transcriber/yaml_deserializer.h"

#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "archive/transcriber.h"
#include "archive/yaml/load.h"
#include "common/i18n/locales.h"
#include "common/util/unicode.h"

namespace bedrock::archive::transcriber {

namespace {

/** @brief "…를 파싱하는 동안 손상" 상태(위치·대상 이름 포함). */
OwnedStatus CorruptedAt(std::string_view what) {
  const std::string_view fmt =
      GetI18nString(locale::StringKey::kStatusCorruptedWhileParsing,
                    locale::IsO6391::kKO, locale::IsO31661::kKR);
  return {TranscriberError::kCorrupted,
          std::vformat(fmt, std::make_format_args(what))};
}

/** @brief 성공 상태. */
OwnedStatus Ok() { return {make_error_code(TranscriberError::kSuccess)}; }

bedrock::Status BorrowStatus(const OwnedStatus& source,
                             std::string& message_storage) {
  message_storage = source.Message();
  const bedrock::ErrorCode code =
      source.Ok() ? bedrock::ErrorCode::kSuccess
                  : static_cast<bedrock::ErrorCode>(source.code.value());
  return bedrock::Status(code, message_storage);
}

struct YAMLDeserializerInput {
  explicit YAMLDeserializerInput(std::string_view source) : input(source) {}
  std::string input;
};

}  // namespace

template <>
struct TextDeserializer<YAMLFormat>::Impl final : private YAMLDeserializerInput,
                                                  public ConstructCore {
  explicit Impl(std::uint16_t id)
      : YAMLDeserializerInput(""), ConstructCore(id, input), machine_id(id) {}
  Impl(std::uint16_t id, std::string_view source)
      : YAMLDeserializerInput(source),
        ConstructCore(id, input),
        machine_id(id) {
    status = BorrowStatus(ConstructCore::Load(), error_storage);
    initialized = status.Ok();
  }
  ~Impl() override;

  bedrock::Status Run(bedrock::archive::Schema& schema, std::string_view name) {
    if (!initialized) {
      return status.Failed() ? status
                             : bedrock::Status(bedrock::ErrorCode::kNotReady);
    }
    if (consumed) {
      status = bedrock::Status(bedrock::ErrorCode::kAlreadyConsumed);
      return status;
    }
    consumed = true;
    status = BorrowStatus(Construct(schema, name), error_storage);
    return status;
  }

  std::uint16_t machine_id;
  std::string error_storage;
  bedrock::Status status = bedrock::Status(bedrock::ErrorCode::kNotReady);
  bool initialized = false;
  bool consumed = false;

 private:
  OwnedStatus LoadRepresentation(std::string_view input, Node& out) final;
  [[nodiscard]] bool IsBinaryScalar(const Node& node) const final;
  void OnDuplicateKeysNotify() final;
};

TextDeserializer<YAMLFormat>::Impl::~Impl() = default;

void TextDeserializer<YAMLFormat>::Impl::OnDuplicateKeysNotify() {
  // multimap 등 중복 키가 의미 있는 스키마다. YAML Load는 중복 쌍을
  // 보존해 적재하고(아래), 유일성 강제는 베이스 Construct가 컨테이너
  // 종류를 보고 판단한다 — 이 신호는 문서화용 마커로 둔다.
}

OwnedStatus TextDeserializer<YAMLFormat>::Impl::LoadRepresentation(
    std::string_view input, Node& out) {
  std::vector<std::uint32_t> buf;
  if (!util::DecodeUtf8(input, buf)) {
    return CorruptedAt("UTF-8");
  }

  // 중복 쌍을 일단 보존해 적재한다 — 유일성 강제는 Construct 단계에서
  // 목표 컨테이너(map vs multimap)를 알 때 수행된다.
  yaml::ComposeOptions options;
  options.allow_duplicate_keys = true;
  yaml::ComposeResult loaded = yaml::Load(buf, options);
  if (!loaded.ok) {
    return CorruptedAt(loaded.error);
  }
  if (loaded.docs.empty()) {
    return CorruptedAt("empty document");
  }
  out = std::move(loaded.docs.front());
  return Ok();
}

bool TextDeserializer<YAMLFormat>::Impl::IsBinaryScalar(const Node& n) const {
  return n.tag == "!!binary";
}

template <>
TextDeserializer<YAMLFormat>::TextDeserializer(std::uint16_t machine_id)
    : impl_(std::make_unique<Impl>(machine_id)) {}
template <>
TextDeserializer<YAMLFormat>::TextDeserializer(std::uint16_t machine_id,
                                               std::string_view input)
    : impl_(std::make_unique<Impl>(machine_id, input)) {}
template <>
TextDeserializer<YAMLFormat>::~TextDeserializer() {}
template <>
TextDeserializer<YAMLFormat>::TextDeserializer(TextDeserializer&&) noexcept =
    default;
template <>
TextDeserializer<YAMLFormat>& TextDeserializer<YAMLFormat>::operator=(
    TextDeserializer&&) noexcept = default;
template <>
bedrock::Status TextDeserializer<YAMLFormat>::Initialize(
    std::string_view input) & {
  impl_ = std::make_unique<Impl>(impl_->machine_id, input);
  return impl_->status;
}
template <>
bedrock::Status TextDeserializer<YAMLFormat>::Load(
    bedrock::archive::Schema& schema, std::string_view name) & {
  return impl_->Run(schema, name);
}
template <>
bedrock::Status TextDeserializer<YAMLFormat>::GetStatus() const noexcept {
  return impl_->status;
}

}  // namespace bedrock::archive::transcriber
