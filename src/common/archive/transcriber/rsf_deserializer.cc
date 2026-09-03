/**
 * @file rsf_deserializer.cc
 * @brief RsfDeserializer public API and private implementation.
 */
#include "common/archive/transcriber/rsf_deserializer.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "archive/rsf/compose.h"
#include "archive/rsf/parse.h"
#include "archive/transcriber.h"

namespace bedrock::archive::transcriber {

namespace {
OwnedStatus Ok() { return {make_error_code(TranscriberError::kSuccess)}; }
bedrock::Status BorrowStatus(const OwnedStatus& source,
                             std::string& message_storage) {
  message_storage = source.Message();
  const bedrock::ErrorCode code =
      source.Ok() ? bedrock::ErrorCode::kSuccess
                  : static_cast<bedrock::ErrorCode>(source.code.value());
  return bedrock::Status(code, message_storage);
}

struct RsfDeserializerInput {
  explicit RsfDeserializerInput(std::string_view source) : input(source) {}
  std::string input;
};

}  // namespace

template <>
struct TextDeserializer<RsfFormat>::Impl final : private RsfDeserializerInput,
                                                 public ConstructCore {
  explicit Impl(std::uint16_t id)
      : RsfDeserializerInput(""), ConstructCore(id, input), machine_id(id) {}
  Impl(std::uint16_t id, std::string_view source)
      : RsfDeserializerInput(source), ConstructCore(id, input), machine_id(id) {
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
};

TextDeserializer<RsfFormat>::Impl::~Impl() = default;

OwnedStatus TextDeserializer<RsfFormat>::Impl::LoadRepresentation(
    std::string_view input, Node& out) {
  const rsf::ParseResult parsed =
      rsf::Parse(std::span<const char>(input.data(), input.size()));
  if (!parsed.ok) {
    return {TranscriberError::kCorrupted, parsed.error};
  }
  rsf::ComposeResult composed = rsf::Compose(
      std::span<const rsf::Event>(parsed.events.data(), parsed.events.size()));
  if (!composed.ok) {
    return {TranscriberError::kCorrupted, composed.error};
  }
  out = std::move(composed.root);
  return Ok();
}

bool TextDeserializer<RsfFormat>::Impl::IsBinaryScalar(const Node& n) const {
  return HasFlag(n.vtype, ValueType::kBinary);
}

template <>
TextDeserializer<RsfFormat>::TextDeserializer(std::uint16_t machine_id)
    : impl_(std::make_unique<Impl>(machine_id)) {}
template <>
TextDeserializer<RsfFormat>::TextDeserializer(std::uint16_t machine_id,
                                              std::string_view input)
    : impl_(std::make_unique<Impl>(machine_id, input)) {}
template <>
TextDeserializer<RsfFormat>::~TextDeserializer() {}
template <>
TextDeserializer<RsfFormat>::TextDeserializer(TextDeserializer&&) noexcept =
    default;
template <>
TextDeserializer<RsfFormat>& TextDeserializer<RsfFormat>::operator=(
    TextDeserializer&&) noexcept = default;
template <>
bedrock::Status TextDeserializer<RsfFormat>::Initialize(
    std::string_view input) & {
  impl_ = std::make_unique<Impl>(impl_->machine_id, input);
  return impl_->status;
}
template <>
bedrock::Status TextDeserializer<RsfFormat>::Load(
    bedrock::archive::Schema& schema, std::string_view name) & {
  return impl_->Run(schema, name);
}
template <>
bedrock::Status TextDeserializer<RsfFormat>::GetStatus() const noexcept {
  return impl_->status;
}

}  // namespace bedrock::archive::transcriber
