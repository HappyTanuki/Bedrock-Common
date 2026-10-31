/**
 * @file yaml_deserializer.cc
 * @brief YAML 텍스트를 representation으로 적재하는 TextDeserializer 구현.
 */
#include "common/archive/transcriber/yaml_deserializer.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "archive/transcriber.h"
#include "archive/yaml/compose.h"
#include "archive/yaml/parse.h"
#include "common/util/unicode.h"

namespace bedrock::archive::transcriber {

template <>
struct TextDeserializer<YAMLFormat>::Impl final : public ConstructCore {
  explicit Impl() : ConstructCore(std::string{}) {}
  explicit Impl(std::string_view source) : ConstructCore(std::string(source)) {
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

  std::string error_storage;
  bedrock::Status status = bedrock::Status(bedrock::ErrorCode::kNotReady);
  bool initialized = false;
  bool consumed = false;

 private:
  OwnedStatus LoadRepresentation(std::string_view input, Node& out) final;
  [[nodiscard]] bool IsBinaryScalar(const Node& node) const final;
};

TextDeserializer<YAMLFormat>::Impl::~Impl() = default;

OwnedStatus TextDeserializer<YAMLFormat>::Impl::LoadRepresentation(
    std::string_view input, Node& out) {
  std::vector<std::uint32_t> code_points;
  if (!util::DecodeUtf8(input, code_points)) {
    return CorruptedAt("UTF-8");
  }

  yaml::ComposeEventBuilder builder;
  std::string parse_error;
  const bool parsed = yaml::Parse(std::span<const std::uint32_t>(code_points),
                                  builder, parse_error);
  yaml::ComposeResult composed = builder.Finish();
  if (!parsed) {
    if (composed.identity_error < 0) {
      return {SnowflakeIdentityError(composed.identity_error),
              "Snowflake identity error"};
    }
    return CorruptedAt(composed.error.empty() ? parse_error : composed.error);
  }
  if (!composed.ok) {
    if (composed.identity_error < 0) {
      return {SnowflakeIdentityError(composed.identity_error),
              "Snowflake identity error"};
    }
    return CorruptedAt(composed.error);
  }
  if (composed.docs.empty()) {
    return CorruptedAt("empty document");
  }
  out = std::move(composed.docs.front());
  return {make_error_code(TranscriberError::kSuccess)};
}

bool TextDeserializer<YAMLFormat>::Impl::IsBinaryScalar(
    const Node& node) const {
  return node.tag == "!!binary";
}

template <>
TextDeserializer<YAMLFormat>::TextDeserializer()
    : impl_(std::make_unique<Impl>()) {}
template <>
TextDeserializer<YAMLFormat>::TextDeserializer(std::string_view input)
    : impl_(std::make_unique<Impl>(input)) {}
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
  impl_ = std::make_unique<Impl>(input);
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
