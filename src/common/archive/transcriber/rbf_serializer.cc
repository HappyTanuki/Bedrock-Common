/**
 * @file rbf_serializer.cc
 * @brief RbfSerializer public API and private implementation.
 *
 * 표현 트리(Node)를 그대로 바이너리(RBF)로 인코딩한다. Node의 종류·null
 * 플래그·vtype 힌트·스칼라 바이트를 손실 없이 실으므로, 역직렬화 시
 * 동일한 트리가 복원되고 베이스 Construct가 YAML과 똑같이 값을 해소한다.
 */
#include "common/archive/transcriber/rbf_serializer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>

#include "archive/rbf/format.h"
#include "archive/transcriber.h"

namespace bedrock::archive::transcriber {

namespace {

/** @brief 성공 상태. */
OwnedStatus Ok() { return {make_error_code(TranscriberError::kSuccess)}; }

void PutU8(std::ostream& output_stream, std::uint8_t byte_value) {
  output_stream.put(static_cast<char>(byte_value));
}

/** @brief 부호 없는 LEB128 varint. */
void PutVarint(std::ostream& output_stream, std::uint64_t encoded_value) {
  while (encoded_value >= 0x80) {
    PutU8(output_stream,
          static_cast<std::uint8_t>((encoded_value & 0x7FU) | 0x80U));
    encoded_value >>= 7;
  }
  PutU8(output_stream, static_cast<std::uint8_t>(encoded_value));
}

void PutRaw(std::ostream& output_stream, const std::string& scalar) {
  output_stream.write(scalar.data(),
                      static_cast<std::streamsize>(scalar.size()));
}

/** @brief 노드 하나를 재귀적으로 RBF로 쓴다. */
void WriteNode(std::ostream& output_stream, const Node& n) {
  switch (n.kind) {
    case Node::Kind::kScalar:
      PutU8(output_stream, static_cast<std::uint8_t>(rbf::BinKind::kScalar));
      PutU8(output_stream, n.null ? std::uint8_t{1} : std::uint8_t{0});
      PutVarint(output_stream, static_cast<std::uint32_t>(n.vtype));
      PutVarint(output_stream, n.scalar.size());
      PutRaw(output_stream, n.scalar);
      break;
    case Node::Kind::kSequence:
      PutU8(output_stream, static_cast<std::uint8_t>(rbf::BinKind::kSequence));
      PutVarint(output_stream, n.items.size());
      for (const Node& item : n.items) {
        WriteNode(output_stream, item);
      }
      break;
    case Node::Kind::kSet:
      PutU8(output_stream, static_cast<std::uint8_t>(rbf::BinKind::kSet));
      PutVarint(output_stream, n.items.size());
      for (const Node& item : n.items) {
        WriteNode(output_stream, item);
      }
      break;
    case Node::Kind::kMapping:
      PutU8(output_stream, static_cast<std::uint8_t>(rbf::BinKind::kMapping));
      PutVarint(output_stream, n.pairs.size());
      for (const Node::Pair& pair : n.pairs) {
        WriteNode(output_stream, pair.key);
        WriteNode(output_stream, pair.value);
      }
      break;
  }
}

bedrock::Status BorrowStatus(const OwnedStatus& source,
                             std::string& message_storage) {
  message_storage = source.Message();
  const bedrock::ErrorCode code =
      source.Ok() ? bedrock::ErrorCode::kSuccess
                  : static_cast<bedrock::ErrorCode>(source.code.value());
  return bedrock::Status(code, message_storage);
}

struct RbfSerializerOutput {
  std::ostringstream stream{std::ios::binary};
};

}  // namespace

template <>
struct BinarySerializer<RbfFormat>::Impl final : private RbfSerializerOutput,
                                                 public RepresentCore {
  explicit Impl(std::uint16_t id) : RepresentCore(id, stream), machine_id(id) {}
  ~Impl() override;

  bedrock::Status Run(bedrock::archive::Schema& schema, std::string_view name) {
    if (consumed) {
      status = bedrock::Status(bedrock::ErrorCode::kAlreadyConsumed);
      return status;
    }
    consumed = true;
    const OwnedStatus represented = Represent(schema, name);
    const OwnedStatus result = represented.Failed() ? represented : Dump();
    status = BorrowStatus(result, error_storage);
    if (status.Failed()) {
      output.clear();
      return status;
    }
    if (!stream.good()) {
      status = bedrock::Status(bedrock::ErrorCode::kError,
                               "archive output stream failed");
      output.clear();
      return status;
    }
    output = stream.str();
    return status;
  }

  std::uint16_t machine_id;
  std::string output;
  std::string error_storage;
  bedrock::Status status = bedrock::Status(bedrock::ErrorCode::kSuccess);
  bool consumed = false;

 private:
  OwnedStatus DumpRepresentation(const Node& root) final;
};

BinarySerializer<RbfFormat>::Impl::~Impl() { static_cast<void>(Dump()); }

OwnedStatus BinarySerializer<RbfFormat>::Impl::DumpRepresentation(
    const Node& root) {
  for (std::uint8_t byte_value : rbf::kBinMagic) {
    PutU8(OutputStream(), byte_value);
  }
  WriteNode(OutputStream(), root);
  if (!OutputStream().good()) {
    return {TranscriberError::kCorrupted, "rbf: write failed"};
  }
  return Ok();
}

template <>
BinarySerializer<RbfFormat>::BinarySerializer(std::uint16_t machine_id)
    : impl_(std::make_unique<Impl>(machine_id)) {}
template <>
BinarySerializer<RbfFormat>::~BinarySerializer() {}
template <>
BinarySerializer<RbfFormat>::BinarySerializer(BinarySerializer&&) noexcept =
    default;
template <>
BinarySerializer<RbfFormat>& BinarySerializer<RbfFormat>::operator=(
    BinarySerializer&&) noexcept = default;
template <>
void BinarySerializer<RbfFormat>::Reset() & {
  impl_ = std::make_unique<Impl>(impl_->machine_id);
}
template <>
bedrock::Status BinarySerializer<RbfFormat>::Dump(
    bedrock::archive::Schema& schema, std::string_view name) & {
  return impl_->Run(schema, name);
}
template <>
std::span<const std::byte> BinarySerializer<RbfFormat>::Output()
    const noexcept {
  return {reinterpret_cast<const std::byte*>(impl_->output.data()),
          impl_->output.size()};
}
template <>
bedrock::Status BinarySerializer<RbfFormat>::GetStatus() const noexcept {
  return impl_->status;
}

}  // namespace bedrock::archive::transcriber
