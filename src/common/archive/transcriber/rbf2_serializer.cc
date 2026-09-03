/**
 * @file rbf2_serializer.cc
 * @brief Rbf2Serializer public API and private implementation.
 */
#include "common/archive/transcriber/rbf2_serializer.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "archive/transcriber.h"
#include "common/archive/visitor.h"

namespace bedrock::archive::transcriber {
namespace {
constexpr std::array<std::uint8_t, 4> kMagic = {0x52, 0x46, 0x32, 0x01};

bedrock::Status BorrowStatus(const OwnedStatus& source,
                             std::string& message_storage) {
  message_storage = source.Message();
  const bedrock::ErrorCode code =
      source.Ok() ? bedrock::ErrorCode::kSuccess
                  : static_cast<bedrock::ErrorCode>(source.code.value());
  return bedrock::Status(code, message_storage);
}
}  // namespace

template <>
struct BinarySerializer<Rbf2Format>::Impl final : public Visitor {
 public:
  explicit Impl(std::uint16_t id)
      : Visitor(id), stream(std::ios::binary), out_(&stream), machine_id(id) {}
  ~Impl() override;

  bedrock::Status Run(bedrock::archive::Schema& schema, std::string_view name);

  [[nodiscard]] bool IsReading() const override { return false; }

  void OnRootBegin(const Field& field) override;
  void OnRootEnd() override;
  void OnObjectBegin(const Field& field) override;
  void OnObjectEnd() override;
  std::size_t OnSeqBegin(const Field& field, std::size_t count) override;
  void OnSeqEnd() override;
  std::size_t OnMapBegin(const Field& field, std::size_t count) override;
  void OnMapEnd() override;
  std::size_t OnSetBegin(const Field& field, std::size_t count) override;
  void OnSetEnd() override;
  std::size_t OnVariantBegin(const Field& field, std::size_t alt_count,
                             std::size_t active_index) override;
  void OnVariantEnd() override;

  Visitor& Visit(const Field& field, bool& value) override;
  Visitor& Visit(const Field& field, std::byte& value) override;
  Visitor& Visit(const Field& field, std::int8_t& value) override;
  Visitor& Visit(const Field& field, std::uint8_t& value) override;
  Visitor& Visit(const Field& field, std::int16_t& value) override;
  Visitor& Visit(const Field& field, std::uint16_t& value) override;
  Visitor& Visit(const Field& field, std::int32_t& value) override;
  Visitor& Visit(const Field& field, std::uint32_t& value) override;
  Visitor& Visit(const Field& field, std::int64_t& value) override;
  Visitor& Visit(const Field& field, std::uint64_t& value) override;
  Visitor& Visit(const Field& field, float& value) override;
  Visitor& Visit(const Field& field, double& value) override;
  Visitor& Visit(const Field& field,
                 std::span<const std::byte>& value) override;
  Visitor& Visit(const Field& field, std::string_view& value) override;

  std::ostringstream stream;
  std::string output;
  std::string error_storage;
  bedrock::Status status = bedrock::Status(bedrock::ErrorCode::kSuccess);
  std::uint16_t machine_id;
  bool consumed = false;

 private:
  /** @brief 프레임 컨텍스트 — 구조체 필드에만 번호를 접두한다. */
  enum class Ctx : std::uint8_t { kFields, kItem, kMap };

  /** @brief 현재 컨텍스트가 kFields면 필드 번호를 접두로 쓴다. */
  void MaybeNumber(const Field& field);
  void U8(std::uint8_t byte_value);
  void Varint(std::uint64_t encoded_value);
  template <class U>
  void LE(U encoded_value) {
    for (std::size_t i = 0; i < sizeof(U); ++i) {
      U8(static_cast<std::uint8_t>(encoded_value & 0xFFU));
      encoded_value = static_cast<U>(encoded_value >> 8);
    }
  }

  std::ostream* out_;
  std::vector<Ctx> ctx_;
};

bedrock::Status BinarySerializer<Rbf2Format>::Impl::Run(
    bedrock::archive::Schema& schema, std::string_view name) {
  if (consumed) {
    status = bedrock::Status(bedrock::ErrorCode::kAlreadyConsumed);
    return status;
  }
  consumed = true;
  (*this)(schema, name);
  const OwnedStatus result;
  status = BorrowStatus(result, error_storage);
  if (!stream.good()) {
    status = bedrock::Status(bedrock::ErrorCode::kError,
                             "archive output stream failed");
    output.clear();
    return status;
  }
  output = stream.str();
  return status;
}

BinarySerializer<Rbf2Format>::Impl::~Impl() = default;

void BinarySerializer<Rbf2Format>::Impl::U8(std::uint8_t byte_value) {
  out_->put(static_cast<char>(byte_value));
}

void BinarySerializer<Rbf2Format>::Impl::Varint(std::uint64_t encoded_value) {
  while (encoded_value >= 0x80) {
    U8(static_cast<std::uint8_t>((encoded_value & 0x7FU) | 0x80U));
    encoded_value >>= 7;
  }
  U8(static_cast<std::uint8_t>(encoded_value));
}

void BinarySerializer<Rbf2Format>::Impl::MaybeNumber(const Field& field) {
  if (!ctx_.empty() && ctx_.back() == Ctx::kFields) {
    Varint(field.number);
  }
}

void BinarySerializer<Rbf2Format>::Impl::OnRootBegin(const Field& /*name*/) {
  for (std::uint8_t byte_value : kMagic) {
    U8(byte_value);
  }
  ctx_.push_back(Ctx::kFields);
}
void BinarySerializer<Rbf2Format>::Impl::OnRootEnd() {
  if (!ctx_.empty()) {
    ctx_.pop_back();
  }
}
void BinarySerializer<Rbf2Format>::Impl::OnObjectBegin(const Field& field) {
  MaybeNumber(field);
  ctx_.push_back(Ctx::kFields);
}
void BinarySerializer<Rbf2Format>::Impl::OnObjectEnd() {
  if (!ctx_.empty()) {
    ctx_.pop_back();
  }
}
std::size_t BinarySerializer<Rbf2Format>::Impl::OnSeqBegin(const Field& field,
                                                           std::size_t count) {
  MaybeNumber(field);
  Varint(count);
  ctx_.push_back(Ctx::kItem);
  return count;
}
void BinarySerializer<Rbf2Format>::Impl::OnSeqEnd() {
  if (!ctx_.empty()) {
    ctx_.pop_back();
  }
}
std::size_t BinarySerializer<Rbf2Format>::Impl::OnMapBegin(const Field& field,
                                                           std::size_t count) {
  MaybeNumber(field);
  Varint(count);
  ctx_.push_back(Ctx::kMap);
  return count;
}
void BinarySerializer<Rbf2Format>::Impl::OnMapEnd() {
  if (!ctx_.empty()) {
    ctx_.pop_back();
  }
}
std::size_t BinarySerializer<Rbf2Format>::Impl::OnSetBegin(const Field& field,
                                                           std::size_t count) {
  MaybeNumber(field);
  Varint(count);
  ctx_.push_back(Ctx::kItem);
  return count;
}
void BinarySerializer<Rbf2Format>::Impl::OnSetEnd() {
  if (!ctx_.empty()) {
    ctx_.pop_back();
  }
}
std::size_t BinarySerializer<Rbf2Format>::Impl::OnVariantBegin(
    const Field& field, std::size_t /*alt_count*/, std::size_t active_index) {
  MaybeNumber(field);          // 필드 번호(구조체 필드일 때)
  Varint(active_index);        // 판별자(활성 대안 인덱스)
  ctx_.push_back(Ctx::kItem);  // 활성 대안 값은 번호 없이 위치 기반
  return active_index;         // 쓰기에선 무시됨
}
void BinarySerializer<Rbf2Format>::Impl::OnVariantEnd() {
  if (!ctx_.empty()) {
    ctx_.pop_back();
  }
}

Visitor& BinarySerializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                   bool& value) {
  MaybeNumber(field);
  U8(value ? std::uint8_t{1} : std::uint8_t{0});
  return *this;
}
Visitor& BinarySerializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                   std::byte& value) {
  MaybeNumber(field);
  U8(std::to_integer<std::uint8_t>(value));
  return *this;
}
Visitor& BinarySerializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                   std::int8_t& value) {
  MaybeNumber(field);
  U8(static_cast<std::uint8_t>(value));
  return *this;
}
Visitor& BinarySerializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                   std::uint8_t& value) {
  MaybeNumber(field);
  U8(value);
  return *this;
}
Visitor& BinarySerializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                   std::int16_t& value) {
  MaybeNumber(field);
  LE<std::uint16_t>(static_cast<std::uint16_t>(value));
  return *this;
}
Visitor& BinarySerializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                   std::uint16_t& value) {
  MaybeNumber(field);
  LE<std::uint16_t>(value);
  return *this;
}
Visitor& BinarySerializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                   std::int32_t& value) {
  MaybeNumber(field);
  LE<std::uint32_t>(static_cast<std::uint32_t>(value));
  return *this;
}
Visitor& BinarySerializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                   std::uint32_t& value) {
  MaybeNumber(field);
  LE<std::uint32_t>(value);
  return *this;
}
Visitor& BinarySerializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                   std::int64_t& value) {
  MaybeNumber(field);
  LE<std::uint64_t>(static_cast<std::uint64_t>(value));
  return *this;
}
Visitor& BinarySerializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                   std::uint64_t& value) {
  MaybeNumber(field);
  LE<std::uint64_t>(value);
  return *this;
}
Visitor& BinarySerializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                   float& value) {
  MaybeNumber(field);
  LE<std::uint32_t>(std::bit_cast<std::uint32_t>(value));
  return *this;
}
Visitor& BinarySerializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                   double& value) {
  MaybeNumber(field);
  LE<std::uint64_t>(std::bit_cast<std::uint64_t>(value));
  return *this;
}
Visitor& BinarySerializer<Rbf2Format>::Impl::Visit(
    const Field& field, std::span<const std::byte>& value) {
  MaybeNumber(field);
  Varint(value.size());
  for (std::byte byte_value : value) {
    U8(std::to_integer<std::uint8_t>(byte_value));
  }
  return *this;
}
Visitor& BinarySerializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                   std::string_view& value) {
  MaybeNumber(field);
  Varint(value.size());
  for (char character : value) {
    U8(static_cast<std::uint8_t>(character));
  }
  return *this;
}

template <>
BinarySerializer<Rbf2Format>::BinarySerializer(std::uint16_t machine_id)
    : impl_(std::make_unique<Impl>(machine_id)) {}
template <>
BinarySerializer<Rbf2Format>::~BinarySerializer() {}
template <>
BinarySerializer<Rbf2Format>::BinarySerializer(BinarySerializer&&) noexcept =
    default;
template <>
BinarySerializer<Rbf2Format>& BinarySerializer<Rbf2Format>::operator=(
    BinarySerializer&&) noexcept = default;
template <>
void BinarySerializer<Rbf2Format>::Reset() & {
  impl_ = std::make_unique<Impl>(impl_->machine_id);
}
template <>
bedrock::Status BinarySerializer<Rbf2Format>::Dump(
    bedrock::archive::Schema& schema, std::string_view name) & {
  return impl_->Run(schema, name);
}
template <>
std::span<const std::byte> BinarySerializer<Rbf2Format>::Output()
    const noexcept {
  return {reinterpret_cast<const std::byte*>(impl_->output.data()),
          impl_->output.size()};
}
template <>
bedrock::Status BinarySerializer<Rbf2Format>::GetStatus() const noexcept {
  return impl_->status;
}

}  // namespace bedrock::archive::transcriber
