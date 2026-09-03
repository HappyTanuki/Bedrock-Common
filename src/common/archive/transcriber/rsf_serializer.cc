/**
 * @file rsf_serializer.cc
 * @brief RsfSerializer public API and private implementation.
 */
#include "common/archive/transcriber/rsf_serializer.h"

#include <cstddef>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>

#include "archive/transcriber.h"

namespace bedrock::archive::transcriber {

namespace {

/** @brief 성공 상태. */
OwnedStatus Ok() { return {make_error_code(TranscriberError::kSuccess)}; }

char HexDigit(unsigned value) {
  return static_cast<char>(value < 10 ? '0' + static_cast<int>(value)
                                      : 'A' + static_cast<int>(value - 10));
}

void WriteEscaped(std::ostream& output_stream, const std::string& scalar) {
  for (char character : scalar) {
    const auto unsigned_character = static_cast<unsigned char>(character);
    if (character == '\\') {
      output_stream << "\\\\";
    } else if (character == '"') {
      output_stream << "\\\"";
    } else if (unsigned_character < 0x20 || unsigned_character == 0x7F) {
      output_stream << "\\x" << HexDigit(unsigned_character >> 4)
                    << HexDigit(unsigned_character & 0x0FU);
    } else {
      output_stream.put(character);
    }
  }
}

/** @brief 스칼라 vtype -> RSF 타입 문자. */
char TypeChar(ValueType value_type) {
  if (HasFlag(value_type, ValueType::kBinary)) {
    return 'Y';
  }
  if (HasFlag(value_type, ValueType::kString)) {
    return 'S';
  }
  if (HasFlag(value_type, ValueType::kBoolean)) {
    return 'B';
  }
  if (HasFlag(value_type, ValueType::kNumber)) {
    return 'N';
  }
  return 'P';  // kNull(plain)
}

void WriteScalar(std::ostream& output_stream, const Node& n) {
  if (n.null) {
    output_stream.put('Z');
    return;
  }
  output_stream.put(TypeChar(n.vtype));
  output_stream.put('"');
  WriteEscaped(output_stream, n.scalar);
  output_stream.put('"');
}

void Present(std::ostream& output_stream, const Node& n, std::size_t indent) {
  const std::string pad(indent * 2, ' ');
  const std::string pad2((indent + 1) * 2, ' ');
  switch (n.kind) {
    case Node::Kind::kScalar:
      WriteScalar(output_stream, n);
      break;
    case Node::Kind::kSequence:
    case Node::Kind::kSet: {
      const bool is_seq = n.kind == Node::Kind::kSequence;
      output_stream << (is_seq ? "[\n" : "(\n");
      for (const Node& item : n.items) {
        output_stream << pad2;
        Present(output_stream, item, indent + 1);
        output_stream << '\n';
      }
      output_stream << pad << (is_seq ? ']' : ')');
      break;
    }
    case Node::Kind::kMapping:
      output_stream << "{\n";
      for (const Node::Pair& pair : n.pairs) {
        output_stream << pad2;
        Present(output_stream, pair.key, indent + 1);
        output_stream << ' ';
        Present(output_stream, pair.value, indent + 1);
        output_stream << '\n';
      }
      output_stream << pad << '}';
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

struct RsfSerializerOutput {
  std::ostringstream stream{std::ios::binary};
};

}  // namespace

template <>
struct TextSerializer<RsfFormat>::Impl final : private RsfSerializerOutput,
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

TextSerializer<RsfFormat>::Impl::~Impl() { static_cast<void>(Dump()); }

OwnedStatus TextSerializer<RsfFormat>::Impl::DumpRepresentation(
    const Node& root) {
  Present(OutputStream(), root, 0);
  OutputStream().put('\n');
  if (!OutputStream().good()) {
    return {TranscriberError::kCorrupted, "rsf: write failed"};
  }
  return Ok();
}

template <>
TextSerializer<RsfFormat>::TextSerializer(std::uint16_t machine_id)
    : impl_(std::make_unique<Impl>(machine_id)) {}
template <>
TextSerializer<RsfFormat>::~TextSerializer() {}
template <>
TextSerializer<RsfFormat>::TextSerializer(TextSerializer&&) noexcept = default;
template <>
TextSerializer<RsfFormat>& TextSerializer<RsfFormat>::operator=(
    TextSerializer&&) noexcept = default;
template <>
void TextSerializer<RsfFormat>::Reset() & {
  impl_ = std::make_unique<Impl>(impl_->machine_id);
}
template <>
bedrock::Status TextSerializer<RsfFormat>::Dump(
    bedrock::archive::Schema& schema, std::string_view name) & {
  return impl_->Run(schema, name);
}
template <>
std::string_view TextSerializer<RsfFormat>::Output() const noexcept {
  return impl_->output;
}
template <>
bedrock::Status TextSerializer<RsfFormat>::GetStatus() const noexcept {
  return impl_->status;
}

}  // namespace bedrock::archive::transcriber
