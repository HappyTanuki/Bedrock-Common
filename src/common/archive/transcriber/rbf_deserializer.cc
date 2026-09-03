/**
 * @file rbf_deserializer.cc
 * @brief RbfDeserializer public API and private implementation.
 *
 * YAML과 같은 계층으로, 스트림 훅 LoadRepresentation는 두 단계를 이어 붙인다:
 *  - Parse(rbf/parse): RBF 바이트열 -> 이벤트 열.
 *  - Compose(rbf/compose): 이벤트 열 -> 표현 트리(Node).
 * 이후 베이스 Deserializer의 Construct가 트리를 목표 C++ 타입으로 해소한다.
 * IsBinaryScalar는 RBF 방언: vtype에 kBinary 플래그가 켜진 스칼라가 바이너리.
 */
#include "common/archive/transcriber/rbf_deserializer.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "archive/rbf/compose.h"
#include "archive/rbf/parse.h"
#include "archive/transcriber.h"

namespace bedrock::archive::transcriber {

namespace {

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

struct RbfDeserializerInput {
  explicit RbfDeserializerInput(std::string_view source) : input(source) {}
  std::string input;
};

}  // namespace

template <>
struct BinaryDeserializer<RbfFormat>::Impl final : private RbfDeserializerInput,
                                                   public ConstructCore {
  explicit Impl(std::uint16_t id)
      : RbfDeserializerInput(""), ConstructCore(id, input), machine_id(id) {}
  Impl(std::uint16_t id, std::string_view source)
      : RbfDeserializerInput(source), ConstructCore(id, input), machine_id(id) {
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

BinaryDeserializer<RbfFormat>::Impl::~Impl() = default;

OwnedStatus BinaryDeserializer<RbfFormat>::Impl::LoadRepresentation(
    std::string_view input, Node& out) {
  const rbf::ParseResult parsed = rbf::Parse(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(input.data()), input.size()));
  if (!parsed.ok) {
    return {TranscriberError::kCorrupted, parsed.error};
  }
  rbf::ComposeResult composed = rbf::Compose(
      std::span<const rbf::Event>(parsed.events.data(), parsed.events.size()));
  if (!composed.ok) {
    return {TranscriberError::kCorrupted, composed.error};
  }
  out = std::move(composed.root);
  return Ok();
}

bool BinaryDeserializer<RbfFormat>::Impl::IsBinaryScalar(const Node& n) const {
  return HasFlag(n.vtype, ValueType::kBinary);
}

template <>
BinaryDeserializer<RbfFormat>::BinaryDeserializer(std::uint16_t machine_id)
    : impl_(std::make_unique<Impl>(machine_id)) {}
template <>
BinaryDeserializer<RbfFormat>::BinaryDeserializer(
    std::uint16_t machine_id, std::span<const std::byte> input)
    : impl_(std::make_unique<Impl>(
          machine_id,
          std::string_view(reinterpret_cast<const char*>(input.data()),
                           input.size()))) {}
template <>
BinaryDeserializer<RbfFormat>::~BinaryDeserializer() {}
template <>
BinaryDeserializer<RbfFormat>::BinaryDeserializer(
    BinaryDeserializer&&) noexcept = default;
template <>
BinaryDeserializer<RbfFormat>& BinaryDeserializer<RbfFormat>::operator=(
    BinaryDeserializer&&) noexcept = default;
template <>
bedrock::Status BinaryDeserializer<RbfFormat>::Initialize(
    std::span<const std::byte> input) & {
  impl_ = std::make_unique<Impl>(
      impl_->machine_id,
      std::string_view(reinterpret_cast<const char*>(input.data()),
                       input.size()));
  return impl_->status;
}
template <>
bedrock::Status BinaryDeserializer<RbfFormat>::Load(
    bedrock::archive::Schema& schema, std::string_view name) & {
  return impl_->Run(schema, name);
}
template <>
bedrock::Status BinaryDeserializer<RbfFormat>::GetStatus() const noexcept {
  return impl_->status;
}

}  // namespace bedrock::archive::transcriber
