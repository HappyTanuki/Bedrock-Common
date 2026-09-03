/**
 * @file yaml_serializer.cc
 * @brief YAMLSerializer public API와 private Impl을 §3.1 Dump에 연결한다.
 */
#include "common/archive/transcriber/yaml_serializer.h"

#include <cstddef>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>

#include "archive/transcriber.h"
#include "archive/yaml/dump.h"

namespace bedrock::archive::transcriber {
namespace {

OwnedStatus DumpFailure(std::string_view detail) {
  return {TranscriberError::kCorrupted, std::string(detail)};
}

bedrock::Status BorrowStatus(const OwnedStatus& source,
                             std::string& message_storage) {
  message_storage = source.Message();
  const bedrock::ErrorCode code =
      source.Ok() ? bedrock::ErrorCode::kSuccess
                  : static_cast<bedrock::ErrorCode>(source.code.value());
  return bedrock::Status(code, message_storage);
}

struct YAMLSerializerOutput {
  std::ostringstream stream{std::ios::binary};
};

}  // namespace

template <>
struct TextSerializer<YAMLFormat>::Impl final : private YAMLSerializerOutput,
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

TextSerializer<YAMLFormat>::Impl::~Impl() { static_cast<void>(Dump()); }

OwnedStatus TextSerializer<YAMLFormat>::Impl::DumpRepresentation(
    const Node& root) {
  const yaml::PresentResult dumped = yaml::Dump(root);
  if (!dumped.ok) {
    return DumpFailure(dumped.error);
  }
  OutputStream() << dumped.text;
  if (!OutputStream().good()) {
    return DumpFailure("YAML presentation stream failed");
  }
  return MutableStatus();
}

template <>
TextSerializer<YAMLFormat>::TextSerializer(std::uint16_t machine_id)
    : impl_(std::make_unique<Impl>(machine_id)) {}
template <>
TextSerializer<YAMLFormat>::~TextSerializer() {}
template <>
TextSerializer<YAMLFormat>::TextSerializer(TextSerializer&&) noexcept = default;
template <>
TextSerializer<YAMLFormat>& TextSerializer<YAMLFormat>::operator=(
    TextSerializer&&) noexcept = default;
template <>
void TextSerializer<YAMLFormat>::Reset() & {
  impl_ = std::make_unique<Impl>(impl_->machine_id);
}
template <>
bedrock::Status TextSerializer<YAMLFormat>::Dump(
    bedrock::archive::Schema& schema, std::string_view name) & {
  return impl_->Run(schema, name);
}
template <>
std::string_view TextSerializer<YAMLFormat>::Output() const noexcept {
  return impl_->output;
}
template <>
bedrock::Status TextSerializer<YAMLFormat>::GetStatus() const noexcept {
  return impl_->status;
}

}  // namespace bedrock::archive::transcriber
