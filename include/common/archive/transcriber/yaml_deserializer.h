#pragma once
#include "transciber.h"

namespace bedrock::archive::transcriber {

class YAMLDeserializer : public Deserializer {
 public:
  YAMLDeserializer(std::uint16_t machine_id, std::istream& input_stream)
      : Deserializer(machine_id, input_stream) {}
  virtual ~YAMLDeserializer() override;

  void OnRootBegin(std::string_view name) final override;
  void OnRootEnd() final override;

  void OnObjectBegin(std::string_view name) final override;
  void OnObjectEnd() final override;

  std::size_t OnSeqBegin(std::string_view name,
                         std::size_t count) final override;
  void OnSeqEnd() final override;

  std::size_t OnMapBegin(std::string_view name,
                         std::size_t count) final override;
  void OnMapEnd() final override;

  // clang-format off
  YAMLDeserializer& Visit(std::string_view name, bool& value) final override;
  YAMLDeserializer& Visit(std::string_view name, std::byte& value) final override;
  YAMLDeserializer& Visit(std::string_view name, std::int8_t& value) final override;
  YAMLDeserializer& Visit(std::string_view name, std::uint8_t& value) final override;
  YAMLDeserializer& Visit(std::string_view name, std::int16_t& value) final override;
  YAMLDeserializer& Visit(std::string_view name, std::uint16_t& value) final override;
  YAMLDeserializer& Visit(std::string_view name, std::int32_t& value) final override;
  YAMLDeserializer& Visit(std::string_view name, std::uint32_t& value) final override;
  YAMLDeserializer& Visit(std::string_view name, std::int64_t& value) final override;
  YAMLDeserializer& Visit(std::string_view name, std::uint64_t& value) final override;
  YAMLDeserializer& Visit(std::string_view name, float& value) final override;
  YAMLDeserializer& Visit(std::string_view name, double& value) final override;

  YAMLDeserializer& Visit(std::string_view name, std::vector<std::byte>& value) final override;

  YAMLDeserializer& Visit(std::string_view name, std::string& value) final override;
  // clang-format on
};

}  // namespace bedrock::archive::transcriber
