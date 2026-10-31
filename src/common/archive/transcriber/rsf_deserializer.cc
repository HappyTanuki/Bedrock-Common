/**
 * @file rsf_deserializer.cc
 * @brief RSF deserializer public API and private representation loader.
 */
#include "common/archive/transcriber/rsf_deserializer.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "archive/transcriber.h"

namespace bedrock::archive::transcriber {
namespace {

OwnedStatus Ok() { return {make_error_code(TranscriberError::kSuccess)}; }

/** @brief RSF serialization을 읽어 representation node를 재귀 구성한다. */
class RsfSerializationLoader {
 public:
  explicit RsfSerializationLoader(std::span<const char> input)
      : input_(input) {}

  OwnedStatus Load(Node& out) {
    if (!LoadSerialization(out, 0)) {
      return Fail(error_);
    }
    SkipWhitespace();
    if (!AtEnd()) {
      return Fail("trailing data");
    }
    return Ok();
  }

 private:
  static constexpr int kMaxDepth = 100;

  static OwnedStatus Fail(std::string_view message) {
    return {TranscriberError::kCorrupted,
            std::string("rsf: ") + std::string(message)};
  }

  bool SetError(const char* message) {
    if (error_.empty()) {
      error_ = message;
    }
    return false;
  }

  [[nodiscard]] bool AtEnd() const { return position_ >= input_.size(); }
  [[nodiscard]] char Peek() const { return AtEnd() ? '\0' : input_[position_]; }

  void SkipWhitespace() {
    while (!AtEnd()) {
      const char character = Peek();
      if (character != ' ' && character != '\t' && character != '\n' &&
          character != '\r') {
        break;
      }
      ++position_;
    }
  }

  static int HexValue(char character) {
    if (character >= '0' && character <= '9') {
      return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
      return 10 + character - 'a';
    }
    if (character >= 'A' && character <= 'F') {
      return 10 + character - 'A';
    }
    return -1;
  }

  bool LoadQuoted(std::string& out) {
    if (Peek() != '"') {
      return SetError("expected opening quote");
    }
    ++position_;
    while (!AtEnd()) {
      const char character = input_[position_++];
      if (character == '"') {
        return true;
      }
      if (character != '\\') {
        out.push_back(character);
        continue;
      }
      if (AtEnd()) {
        break;
      }
      const char escape = input_[position_++];
      if (escape == '\\' || escape == '"') {
        out.push_back(escape);
      } else if (escape == 'x') {
        if (position_ + 1 >= input_.size()) {
          return SetError("bad \\x escape");
        }
        const int high = HexValue(input_[position_]);
        const int low = HexValue(input_[position_ + 1]);
        if (high < 0 || low < 0) {
          return SetError("bad hex digit");
        }
        position_ += 2;
        out.push_back(static_cast<char>((high << 4) | low));
      } else {
        return SetError("unknown escape");
      }
    }
    return SetError("unterminated string");
  }

  bool LoadSequence(Node& out, char close, int depth) {
    while (true) {
      SkipWhitespace();
      if (AtEnd()) {
        return SetError("unterminated container");
      }
      if (Peek() == close) {
        ++position_;
        return true;
      }
      Node item;
      if (!LoadSerialization(item, depth + 1)) {
        return false;
      }
      out.items.push_back(std::move(item));
    }
  }

  bool LoadMapping(Node& out, int depth) {
    while (true) {
      SkipWhitespace();
      if (AtEnd()) {
        return SetError("unterminated container");
      }
      if (Peek() == '}') {
        ++position_;
        return true;
      }

      Node key;
      if (!LoadSerialization(key, depth + 1)) {
        return false;
      }
      SkipWhitespace();
      if (AtEnd()) {
        return SetError("unterminated container");
      }
      if (Peek() == '}') {
        return SetError("odd mapping child count");
      }

      Node value;
      if (!LoadSerialization(value, depth + 1)) {
        return false;
      }
      out.pairs.push_back({std::move(key), std::move(value)});
    }
  }

  bool LoadSerialization(Node& out, int depth) {
    if (depth > kMaxDepth) {
      return SetError("max depth");
    }
    SkipWhitespace();
    if (AtEnd()) {
      return SetError("unexpected end");
    }

    const char token = Peek();
    if (token == '[' || token == '(' || token == '{') {
      ++position_;
      if (token == '{') {
        out.kind = Node::Kind::kMapping;
        return LoadMapping(out, depth);
      }
      out.kind = token == '[' ? Node::Kind::kSequence : Node::Kind::kSet;
      return LoadSequence(out, token == '[' ? ']' : ')', depth);
    }

    out.kind = Node::Kind::kScalar;
    if (token == 'Z') {
      ++position_;
      out.null = true;
      return true;
    }

    if (token == 'P') {
      out.vtype = ValueType::kNull;
    } else if (token == 'N') {
      out.vtype = ValueType::kNumber;
    } else if (token == 'S') {
      out.vtype = ValueType::kString;
    } else if (token == 'B') {
      out.vtype = ValueType::kBoolean;
    } else if (token == 'Y') {
      out.vtype = ValueType::kBinary;
    } else {
      return SetError("unexpected token");
    }
    ++position_;
    return LoadQuoted(out.scalar);
  }

  std::span<const char> input_;
  std::size_t position_ = 0;
  std::string error_;
};

}  // namespace

template <>
struct TextDeserializer<RsfFormat>::Impl final : public ConstructCore {
  explicit Impl() : ConstructCore(std::string{}) {}
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

TextDeserializer<RsfFormat>::Impl::~Impl() = default;

OwnedStatus TextDeserializer<RsfFormat>::Impl::LoadRepresentation(
    std::string_view input, Node& out) {
  RsfSerializationLoader loader(
      std::span<const char>(input.data(), input.size()));
  return loader.Load(out);
}

bool TextDeserializer<RsfFormat>::Impl::IsBinaryScalar(const Node& node) const {
  return HasFlag(node.vtype, ValueType::kBinary);
}

template <>
TextDeserializer<RsfFormat>::TextDeserializer()
    : impl_(std::make_unique<Impl>()) {}
template <>
TextDeserializer<RsfFormat>::TextDeserializer(
                                              std::string_view input)
    : impl_(std::make_unique<Impl>(input)) {}
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
  impl_ = std::make_unique<Impl>(input);
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
