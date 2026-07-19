#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bedrock::archive {

struct Schema;

class Visitor {
 public:
  Visitor(std::uint16_t machine_id) : _machine_id(machine_id) {}
  virtual ~Visitor();

  void operator()(Schema& root, std::string_view name = "");

  virtual Visitor& OnRootBegin(std::string_view name) = 0;
  virtual Visitor& OnRootEnd() = 0;

  virtual Visitor& OnObjectBegin(std::string_view name) = 0;
  virtual Visitor& OnObjectEnd() = 0;

  // clang-format off
  virtual Visitor& Visit(std::string_view name, bool& value) = 0;
  virtual Visitor& Visit(std::string_view name, std::byte& value) = 0;
  virtual Visitor& Visit(std::string_view name, std::int8_t& value) = 0;
  virtual Visitor& Visit(std::string_view name, std::uint8_t& value) = 0;
  virtual Visitor& Visit(std::string_view name, std::int16_t& value) = 0;
  virtual Visitor& Visit(std::string_view name, std::uint16_t& value) = 0;
  virtual Visitor& Visit(std::string_view name, std::int32_t& value) = 0;
  virtual Visitor& Visit(std::string_view name, std::uint32_t& value) = 0;
  virtual Visitor& Visit(std::string_view name, std::int64_t& value) = 0;
  virtual Visitor& Visit(std::string_view name, std::uint64_t& value) = 0;
  virtual Visitor& Visit(std::string_view name, float& value) = 0;
  virtual Visitor& Visit(std::string_view name, double& value) = 0;

  virtual Visitor& Visit(std::string_view name, std::string& value) = 0;

  Visitor& Visit(std::string_view name, Schema& value);
  // clang-format on
 protected:
  std::uint16_t _machine_id;

  std::size_t _indent_size = 2;
};

}  // namespace bedrock::archive
