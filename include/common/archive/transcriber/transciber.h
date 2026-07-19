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

enum class ObjectType { kObject, kValue };

enum class ValueType { kArray, kString, kNumber, kBoolean, kNull };

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

  virtual Status Flush() = 0;

 protected:
  bool IsSeekable(std::ostream& _output_stream);

  virtual void InsertAppropriateToken(Object& obj) = 0;

  std::ostream& _output_stream;

  std::stack<Object> _objects;
  std::unique_ptr<Object> _root_object;

  Status _status;
};

}  // namespace bedrock::archive::transcriber

template <>
struct std::is_error_code_enum<bedrock::archive::transcriber::TranscriberError>
    : std::true_type {};
