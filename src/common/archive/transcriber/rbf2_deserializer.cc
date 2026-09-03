/**
 * @file rbf2_deserializer.cc
 * @brief Rbf2Deserializer public API and private implementation.
 */
#include "common/archive/transcriber/rbf2_deserializer.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "archive/transcriber.h"
#include "common/archive/visitor.h"

namespace bedrock::archive::transcriber {
namespace {
constexpr std::array<std::uint8_t, 4> kMagic = {0x52, 0x46, 0x32, 0x01};
constexpr std::size_t kFail = std::numeric_limits<std::size_t>::max();

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
struct BinaryDeserializer<Rbf2Format>::Impl final : public Visitor {
 public:
  explicit Impl(std::uint16_t id);
  Impl(std::uint16_t id, std::string_view input);
  ~Impl() override;

  bedrock::Status Run(bedrock::archive::Schema& schema, std::string_view name);
  [[nodiscard]] std::uint16_t MachineId() const noexcept { return machine_id; }
  [[nodiscard]] bedrock::Status ApiStatus() const noexcept {
    return api_status;
  }

  /** @brief 현재까지의 디코딩 상태. */
  [[nodiscard]] const OwnedStatus& GetStatus() const { return status_; }
  OwnedStatus& GetStatus() { return status_; }

  [[nodiscard]] bool IsReading() const override { return true; }

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

 private:
  OwnedStatus status_;
  enum class Ctx : std::uint8_t { kFields, kItem, kMap };

  void Fail(const char* message);
  bool Need(std::size_t byte_count);
  std::uint8_t U8();
  std::uint64_t Varint();
  std::size_t BoundedLen();
  template <class U>
  U LE() {
    if (!Need(sizeof(U))) {
      return 0;
    }
    U value = 0;
    for (std::size_t i = 0; i < sizeof(U); ++i) {
      value =
          static_cast<U>(value | (static_cast<U>(data_[pos_ + i]) << (8 * i)));
    }
    pos_ += sizeof(U);
    return value;
  }
  /** @brief 현재 컨텍스트가 kFields면 접두 번호를 읽어 기대값과 대조한다. */
  void ExpectNumber(const Field& field);

  std::uint16_t machine_id;
  std::string input_storage;
  std::string error_storage;
  bedrock::Status api_status = bedrock::Status(bedrock::ErrorCode::kNotReady);
  bool initialized = false;
  bool consumed = false;
  std::span<const std::uint8_t> data_;
  std::size_t pos_ = 0;
  std::vector<Ctx> ctx_;
};

bedrock::Status BinaryDeserializer<Rbf2Format>::Impl::Run(
    bedrock::archive::Schema& schema, std::string_view name) {
  if (!initialized) {
    return api_status.Failed() ? api_status
                               : bedrock::Status(bedrock::ErrorCode::kNotReady);
  }
  if (consumed) {
    api_status = bedrock::Status(bedrock::ErrorCode::kAlreadyConsumed);
    return api_status;
  }
  consumed = true;
  (*this)(schema, name);
  api_status = BorrowStatus(status_, error_storage);
  return api_status;
}

BinaryDeserializer<Rbf2Format>::Impl::Impl(std::uint16_t id)
    : Visitor(id), machine_id(id) {}

BinaryDeserializer<Rbf2Format>::Impl::Impl(std::uint16_t id,
                                           std::string_view input)
    : Visitor(id),
      machine_id(id),
      input_storage(input),
      data_(reinterpret_cast<const std::uint8_t*>(input_storage.data()),
            input_storage.size()) {
  bool magic_ok = data_.size() >= 4;
  for (std::size_t i = 0; magic_ok && i < 4; ++i) {
    magic_ok = data_[i] == kMagic[i];
  }
  if (!magic_ok) {
    Fail("magic mismatch");
  } else {
    pos_ = 4;
  }
  api_status = BorrowStatus(status_, error_storage);
  initialized = api_status.Ok();
}
BinaryDeserializer<Rbf2Format>::Impl::~Impl() = default;

void BinaryDeserializer<Rbf2Format>::Impl::Fail(const char* message) {
  if (status_.Ok()) {
    status_ = OwnedStatus(TranscriberError::kCorrupted,
                          std::string("rbf2: ") + message);
  }
}
bool BinaryDeserializer<Rbf2Format>::Impl::Need(std::size_t byte_count) {
  if (pos_ + byte_count > data_.size()) {
    Fail("underflow");
    return false;
  }
  return true;
}
std::uint8_t BinaryDeserializer<Rbf2Format>::Impl::U8() {
  if (!Need(1)) {
    return 0;
  }
  return data_[pos_++];
}
std::uint64_t BinaryDeserializer<Rbf2Format>::Impl::Varint() {
  std::uint64_t result = 0;
  int shift = 0;
  for (;;) {
    if (!Need(1)) {
      return 0;
    }
    const std::uint8_t byte_value = data_[pos_++];
    result |= static_cast<std::uint64_t>(byte_value & 0x7FU) << shift;
    if ((byte_value & 0x80U) == 0) {
      break;
    }
    shift += 7;
    if (shift > 63) {
      Fail("varint too long");
      return 0;
    }
  }
  return result;
}
std::size_t BinaryDeserializer<Rbf2Format>::Impl::BoundedLen() {
  const std::uint64_t length = Varint();
  if (status_.Failed()) {
    return 0;
  }
  if (length > data_.size() - pos_) {
    Fail("length exceeds input");
    return 0;
  }
  return static_cast<std::size_t>(length);
}
void BinaryDeserializer<Rbf2Format>::Impl::ExpectNumber(const Field& field) {
  if (ctx_.empty() || ctx_.back() != Ctx::kFields) {
    return;
  }
  const std::uint64_t got = Varint();
  if (status_.Failed()) {
    return;
  }
  if (got != field.number) {
    Fail("field number mismatch");
  }
}

void BinaryDeserializer<Rbf2Format>::Impl::OnRootBegin(const Field& /*name*/) {
  ctx_.push_back(Ctx::kFields);
}
void BinaryDeserializer<Rbf2Format>::Impl::OnRootEnd() {
  if (!ctx_.empty()) {
    ctx_.pop_back();
  }
}
void BinaryDeserializer<Rbf2Format>::Impl::OnObjectBegin(const Field& field) {
  ExpectNumber(field);
  ctx_.push_back(Ctx::kFields);
}
void BinaryDeserializer<Rbf2Format>::Impl::OnObjectEnd() {
  if (!ctx_.empty()) {
    ctx_.pop_back();
  }
}
std::size_t BinaryDeserializer<Rbf2Format>::Impl::OnSeqBegin(
    const Field& field, std::size_t /*count*/) {
  ExpectNumber(field);
  const std::size_t length = BoundedLen();
  if (status_.Failed()) {
    return kFail;
  }
  ctx_.push_back(Ctx::kItem);
  return length;
}
void BinaryDeserializer<Rbf2Format>::Impl::OnSeqEnd() {
  if (!ctx_.empty()) {
    ctx_.pop_back();
  }
}
std::size_t BinaryDeserializer<Rbf2Format>::Impl::OnMapBegin(
    const Field& field, std::size_t /*count*/) {
  ExpectNumber(field);
  const std::size_t length = BoundedLen();
  if (status_.Failed()) {
    return kFail;
  }
  ctx_.push_back(Ctx::kMap);
  return length;
}
void BinaryDeserializer<Rbf2Format>::Impl::OnMapEnd() {
  if (!ctx_.empty()) {
    ctx_.pop_back();
  }
}
std::size_t BinaryDeserializer<Rbf2Format>::Impl::OnSetBegin(
    const Field& field, std::size_t /*count*/) {
  ExpectNumber(field);
  const std::size_t length = BoundedLen();
  if (status_.Failed()) {
    return kFail;
  }
  ctx_.push_back(Ctx::kItem);
  return length;
}
void BinaryDeserializer<Rbf2Format>::Impl::OnSetEnd() {
  if (!ctx_.empty()) {
    ctx_.pop_back();
  }
}
std::size_t BinaryDeserializer<Rbf2Format>::Impl::OnVariantBegin(
    const Field& field, std::size_t alt_count, std::size_t /*active_index*/) {
  ExpectNumber(field);  // 필드 번호(구조체 필드일 때) — 현재 컨텍스트 기준
  std::size_t idx = 0;
  if (!status_.Failed()) {
    const std::uint64_t got = Varint();
    if (!status_.Failed()) {
      if (got >= alt_count) {
        Fail("variant index out of range");
      } else {
        idx = static_cast<std::size_t>(got);
      }
    }
  }
  ctx_.push_back(Ctx::kItem);  // 항상 push -> OnVariantEnd와 균형(실패해도)
  return idx;
}
void BinaryDeserializer<Rbf2Format>::Impl::OnVariantEnd() {
  if (!ctx_.empty()) {
    ctx_.pop_back();
  }
}

Visitor& BinaryDeserializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                     bool& value) {
  ExpectNumber(field);
  if (!status_.Failed()) {
    value = U8() != 0;
  }
  return *this;
}
Visitor& BinaryDeserializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                     std::byte& value) {
  ExpectNumber(field);
  if (!status_.Failed()) {
    value = static_cast<std::byte>(U8());
  }
  return *this;
}
Visitor& BinaryDeserializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                     std::int8_t& value) {
  ExpectNumber(field);
  if (!status_.Failed()) {
    value = static_cast<std::int8_t>(U8());
  }
  return *this;
}
Visitor& BinaryDeserializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                     std::uint8_t& value) {
  ExpectNumber(field);
  if (!status_.Failed()) {
    value = U8();
  }
  return *this;
}
Visitor& BinaryDeserializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                     std::int16_t& value) {
  ExpectNumber(field);
  if (!status_.Failed()) {
    value = static_cast<std::int16_t>(LE<std::uint16_t>());
  }
  return *this;
}
Visitor& BinaryDeserializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                     std::uint16_t& value) {
  ExpectNumber(field);
  if (!status_.Failed()) {
    value = LE<std::uint16_t>();
  }
  return *this;
}
Visitor& BinaryDeserializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                     std::int32_t& value) {
  ExpectNumber(field);
  if (!status_.Failed()) {
    value = static_cast<std::int32_t>(LE<std::uint32_t>());
  }
  return *this;
}
Visitor& BinaryDeserializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                     std::uint32_t& value) {
  ExpectNumber(field);
  if (!status_.Failed()) {
    value = LE<std::uint32_t>();
  }
  return *this;
}
Visitor& BinaryDeserializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                     std::int64_t& value) {
  ExpectNumber(field);
  if (!status_.Failed()) {
    value = static_cast<std::int64_t>(LE<std::uint64_t>());
  }
  return *this;
}
Visitor& BinaryDeserializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                     std::uint64_t& value) {
  ExpectNumber(field);
  if (!status_.Failed()) {
    value = LE<std::uint64_t>();
  }
  return *this;
}
Visitor& BinaryDeserializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                     float& value) {
  ExpectNumber(field);
  if (!status_.Failed()) {
    value = std::bit_cast<float>(LE<std::uint32_t>());
  }
  return *this;
}
Visitor& BinaryDeserializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                     double& value) {
  ExpectNumber(field);
  if (!status_.Failed()) {
    value = std::bit_cast<double>(LE<std::uint64_t>());
  }
  return *this;
}
Visitor& BinaryDeserializer<Rbf2Format>::Impl::Visit(
    const Field& field, std::span<const std::byte>& value) {
  ExpectNumber(field);
  const std::size_t length = BoundedLen();
  if (status_.Failed()) {
    return *this;
  }
  value = std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(data_.data() + pos_), length);
  pos_ += length;
  return *this;
}
Visitor& BinaryDeserializer<Rbf2Format>::Impl::Visit(const Field& field,
                                                     std::string_view& value) {
  ExpectNumber(field);
  const std::size_t length = BoundedLen();
  if (status_.Failed()) {
    return *this;
  }
  value = std::string_view(reinterpret_cast<const char*>(data_.data() + pos_),
                           length);
  pos_ += length;
  return *this;
}

template <>
BinaryDeserializer<Rbf2Format>::BinaryDeserializer(std::uint16_t machine_id)
    : impl_(std::make_unique<Impl>(machine_id)) {}
template <>
BinaryDeserializer<Rbf2Format>::BinaryDeserializer(
    std::uint16_t machine_id, std::span<const std::byte> input)
    : impl_(std::make_unique<Impl>(
          machine_id,
          std::string_view(reinterpret_cast<const char*>(input.data()),
                           input.size()))) {}
template <>
BinaryDeserializer<Rbf2Format>::~BinaryDeserializer() {}
template <>
BinaryDeserializer<Rbf2Format>::BinaryDeserializer(
    BinaryDeserializer&&) noexcept = default;
template <>
BinaryDeserializer<Rbf2Format>& BinaryDeserializer<Rbf2Format>::operator=(
    BinaryDeserializer&&) noexcept = default;
template <>
bedrock::Status BinaryDeserializer<Rbf2Format>::Initialize(
    std::span<const std::byte> input) & {
  impl_ = std::make_unique<Impl>(
      impl_->MachineId(),
      std::string_view(reinterpret_cast<const char*>(input.data()),
                       input.size()));
  return impl_->ApiStatus();
}
template <>
bedrock::Status BinaryDeserializer<Rbf2Format>::Load(
    bedrock::archive::Schema& schema, std::string_view name) & {
  return impl_->Run(schema, name);
}
template <>
bedrock::Status BinaryDeserializer<Rbf2Format>::GetStatus() const noexcept {
  return impl_->ApiStatus();
}

}  // namespace bedrock::archive::transcriber
