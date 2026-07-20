#pragma once
#include <memory>
#include <stack>

#include "common/archive/visitor.h"
#include "common/data_types/snowflake.h"
#include "common/data_types/status.h"

namespace bedrock::archive::transcriber {

enum class TranscriberError : std::uint32_t {
  kSuccess = 0,
  kNoENT = 1,
  kNullStream = 2,
  kCorrupted = 3,
  kError = 0xFFFFFFFF,
};

const std::error_category& TranscriberCategory() noexcept;
std::error_code make_error_code(TranscriberError e) noexcept;

enum class ObjectType { kRoot, kObject, kValue };

enum class ValueType : std::uint32_t {
  kNull = 0,
  kSequence = 1,
  kMap = 1 << 1,
  kSet = 1 << 2,
  kString = 1 << 3,
  kNumber = 1 << 4,
  kBoolean = 1 << 5,
  kBinary = 1 << 6
};

constexpr ValueType operator|(ValueType a, ValueType b) {
  return static_cast<ValueType>(static_cast<std::uint32_t>(a) |
                                static_cast<std::uint32_t>(b));
}
constexpr ValueType& operator|=(ValueType& a, ValueType b) { return a = a | b; }
constexpr ValueType operator&(ValueType a, ValueType b) {
  return static_cast<ValueType>(static_cast<std::uint32_t>(a) &
                                static_cast<std::uint32_t>(b));
}
// 플래그가 켜져 있는지
constexpr bool HasFlag(ValueType v, ValueType f) {
  return (static_cast<std::uint32_t>(v) & static_cast<std::uint32_t>(f)) != 0;
}

struct Object {
  Snowflake id;
  ObjectType object_type;
  ValueType value_type;

  std::string name;

  std::string begin_token;
  std::vector<Object> children;
  std::vector<std::byte> value;
  std::string end_token;
};

class Deserializer : public Visitor {
 public:
  Deserializer(std::uint16_t machine_id, std::istream& input_stream)
      : Visitor(machine_id),
        status(make_error_code(TranscriberError::kSuccess)),
        _input_stream(input_stream) {}
  virtual ~Deserializer() override;

  // 방향 판별: 역직렬화기는 읽기. 포맷별 클래스는 이걸 다시 구현할 필요 없음.
  bool IsReading() const final override { return true; }

  Status status;

 protected:
  bool IsSeekable(std::istream& _input_stream);

  std::istream& _input_stream;

  std::stack<Object> _objects;
  std::unique_ptr<Object> _root_object;
};

class Serializer : public Visitor {
 public:
  Serializer(std::uint16_t machine_id, std::ostream& output_stream)
      : Visitor(machine_id),
        _output_stream(output_stream),
        _status(make_error_code(TranscriberError::kSuccess)) {}
  virtual ~Serializer() override;

  // 방향 판별: 직렬화기는 쓰기. 포맷별 클래스는 이걸 다시 구현할 필요 없음.
  bool IsReading() const final override { return false; }

  virtual Status Flush() = 0;

 protected:
  bool IsSeekable(std::ostream& _output_stream);

  virtual void InjectAppropriateToken(Object& obj) = 0;

  std::ostream& _output_stream;

  std::stack<Object> _objects;
  std::unique_ptr<Object> _root_object;

  std::string _pending_key;

  Status _status;
};

}  // namespace bedrock::archive::transcriber

template <>
struct std::is_error_code_enum<bedrock::archive::transcriber::TranscriberError>
    : std::true_type {};
