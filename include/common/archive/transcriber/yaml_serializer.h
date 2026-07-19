#pragma once
#include "transciber.h"

namespace bedrock::archive::transcriber {

class YAMLSerializer : public Serializer {
 public:
  YAMLSerializer(std::uint16_t machine_id, std::ostream& output_stream)
      : Serializer(machine_id, output_stream) {}
  virtual ~YAMLSerializer() override;

  Status Flush() final override;

  void InsertAppropriateToken(Object& obj) final override;

  YAMLSerializer& OnRootBegin(std::string_view name) final override;
  YAMLSerializer& OnRootEnd() final override;

  YAMLSerializer& OnObjectBegin(std::string_view name) final override;
  YAMLSerializer& OnObjectEnd() final override;

  // clang-format off
  YAMLSerializer& Visit(std::string_view name, bool& value)  final override;
  YAMLSerializer& Visit(std::string_view name, std::byte& value)  final override;
  YAMLSerializer& Visit(std::string_view name, std::int8_t& value)  final override;
  YAMLSerializer& Visit(std::string_view name, std::uint8_t& value)  final override;
  YAMLSerializer& Visit(std::string_view name, std::int16_t& value)  final override;
  YAMLSerializer& Visit(std::string_view name, std::uint16_t& value)  final override;
  YAMLSerializer& Visit(std::string_view name, std::int32_t& value)  final override;
  YAMLSerializer& Visit(std::string_view name, std::uint32_t& value)  final override;
  YAMLSerializer& Visit(std::string_view name, std::int64_t& value)  final override;
  YAMLSerializer& Visit(std::string_view name, std::uint64_t& value)  final override;
  YAMLSerializer& Visit(std::string_view name, float& value)  final override;
  YAMLSerializer& Visit(std::string_view name, double& value)  final override;

  YAMLSerializer& Visit(std::string_view name, std::string& value)  final override;
  // clang-format on
};

}  // namespace bedrock::archive::transcriber
