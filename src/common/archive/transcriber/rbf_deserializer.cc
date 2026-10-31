/**
 * @file rbf_deserializer.cc
 * @brief RBF deserializer public API and private representation loader.
 */
#include "common/archive/transcriber/rbf_deserializer.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "archive/rbf/format.h"
#include "archive/transcriber.h"

namespace bedrock::archive::transcriber {
namespace {

/** @brief 성공 상태. */
OwnedStatus Ok() { return {make_error_code(TranscriberError::kSuccess)}; }

/** @brief RBF serialization을 읽어 representation node를 재귀 구성한다. */
class RbfSerializationLoader {
 public:
  explicit RbfSerializationLoader(std::span<const std::uint8_t> input)
      : input_(input) {}

  OwnedStatus Load(Node& out) {
    if (input_.size() < rbf::kBinMagic.size()) {
      return Fail("magic mismatch");
    }
    for (std::size_t index = 0; index < rbf::kBinMagic.size(); ++index) {
      if (input_[index] != rbf::kBinMagic[index]) {
        return Fail("magic mismatch");
      }
    }
    position_ = rbf::kBinMagic.size();
    if (!LoadSerialization(out, 0)) {
      return Fail(error_);
    }
    return Ok();
  }

 private:
  static constexpr int kMaxDepth = 100;

  static OwnedStatus Fail(std::string_view message) {
    return {TranscriberError::kCorrupted,
            std::string("rbf: ") + std::string(message)};
  }

  bool SetError(const char* message) {
    if (error_.empty()) {
      error_ = message;
    }
    return false;
  }

  bool Need(std::size_t count) {
    return count <= input_.size() - position_ || SetError("underflow");
  }

  std::uint8_t U8() {
    if (!Need(1)) {
      return 0;
    }
    return input_[position_++];
  }

  std::uint64_t Varint() {
    std::uint64_t result = 0;
    int shift = 0;
    for (;;) {
      if (!Need(1)) {
        return 0;
      }
      const std::uint8_t byte_value = input_[position_++];
      result |= static_cast<std::uint64_t>(byte_value & 0x7FU) << shift;
      if ((byte_value & 0x80U) == 0) {
        return result;
      }
      shift += 7;
      if (shift > 63) {
        SetError("varint too long");
        return 0;
      }
    }
  }

  std::size_t BoundedLength() {
    const std::uint64_t length = Varint();
    if (!error_.empty()) {
      return 0;
    }
    if (length > input_.size() - position_) {
      SetError("length exceeds input");
      return 0;
    }
    return static_cast<std::size_t>(length);
  }

  std::string Raw(std::size_t size) {
    if (!Need(size)) {
      return {};
    }
    const char* begin =
        reinterpret_cast<const char*>(input_.data() + position_);
    position_ += size;
    return std::string(begin, size);
  }

  bool LoadSerialization(Node& out, int depth) {
    if (depth > kMaxDepth) {
      return SetError("max depth");
    }
    if (!Need(1)) {
      return false;
    }

    const std::uint8_t kind = U8();
    if (kind == static_cast<std::uint8_t>(rbf::BinKind::kScalar)) {
      out.kind = Node::Kind::kScalar;
      out.null = U8() != 0;
      out.vtype = static_cast<ValueType>(static_cast<std::uint32_t>(Varint()));
      const std::size_t length = BoundedLength();
      if (!error_.empty()) {
        return false;
      }
      out.scalar = Raw(length);
      return error_.empty();
    }

    if (kind == static_cast<std::uint8_t>(rbf::BinKind::kSequence) ||
        kind == static_cast<std::uint8_t>(rbf::BinKind::kSet)) {
      out.kind = kind == static_cast<std::uint8_t>(rbf::BinKind::kSequence)
                     ? Node::Kind::kSequence
                     : Node::Kind::kSet;
      const std::size_t count = BoundedLength();
      if (!error_.empty()) {
        return false;
      }
      out.items.reserve(count);
      for (std::size_t index = 0; index < count; ++index) {
        Node item;
        if (!LoadSerialization(item, depth + 1)) {
          return false;
        }
        out.items.push_back(std::move(item));
      }
      return true;
    }

    if (kind == static_cast<std::uint8_t>(rbf::BinKind::kMapping)) {
      out.kind = Node::Kind::kMapping;
      const std::size_t count = BoundedLength();
      if (!error_.empty()) {
        return false;
      }
      out.pairs.reserve(count);
      for (std::size_t index = 0; index < count; ++index) {
        Node key;
        Node value;
        if (!LoadSerialization(key, depth + 1) ||
            !LoadSerialization(value, depth + 1)) {
          return false;
        }
        out.pairs.push_back({std::move(key), std::move(value)});
      }
      return true;
    }

    return SetError("bad kind");
  }

  std::span<const std::uint8_t> input_;
  std::size_t position_ = 0;
  std::string error_;
};

}  // namespace

template <>
struct BinaryDeserializer<RbfFormat>::Impl final : public ConstructCore {
  Impl() : ConstructCore(std::string{}) {}
  explicit Impl(std::string_view source)
      : ConstructCore(std::string(source)) {
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

BinaryDeserializer<RbfFormat>::Impl::~Impl() = default;

OwnedStatus BinaryDeserializer<RbfFormat>::Impl::LoadRepresentation(
    std::string_view input, Node& out) {
  RbfSerializationLoader loader(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(input.data()), input.size()));
  return loader.Load(out);
}

bool BinaryDeserializer<RbfFormat>::Impl::IsBinaryScalar(
    const Node& node) const {
  return HasFlag(node.vtype, ValueType::kBinary);
}

template <>
BinaryDeserializer<RbfFormat>::BinaryDeserializer()
    : impl_(std::make_unique<Impl>()) {}
template <>
BinaryDeserializer<RbfFormat>::BinaryDeserializer(std::span<const std::byte> input)
    : impl_(std::make_unique<Impl>(
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
