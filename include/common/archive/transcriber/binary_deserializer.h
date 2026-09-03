/** @file binary_deserializer.h @brief Public binary deserializer template. */
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "common/archive.h"
#include "common/data_types/status.h"

namespace bedrock::archive::transcriber {

/**
 * @brief Public API boundary for a format-specific binary deserializer.
 *
 * The pImpl keeps the format implementation, copied input, parsed
 * representation, and diagnostic storage owned by the library. Owning STL
 * objects and internal representation types therefore do not cross the DLL
 * boundary. Initialize() copies the supplied span during the call.
 */
template <typename Format>
class BinaryDeserializer {
 public:
  explicit BinaryDeserializer(std::uint16_t machine_id = 0);
  BinaryDeserializer(std::uint16_t machine_id,
                     std::span<const std::byte> input);
  ~BinaryDeserializer();
  BinaryDeserializer(const BinaryDeserializer&) = delete;
  BinaryDeserializer& operator=(const BinaryDeserializer&) = delete;
  BinaryDeserializer(BinaryDeserializer&&) noexcept;
  BinaryDeserializer& operator=(BinaryDeserializer&&) noexcept;

  [[nodiscard]] bedrock::Status Initialize(std::span<const std::byte> input) &;
  [[nodiscard]] bedrock::Status Load(bedrock::archive::Schema& schema,
                                     std::string_view name = "") &;
  bedrock::Status Load(bedrock::archive::Schema&,
                       std::string_view = "") && = delete;
  [[nodiscard]] bedrock::Status GetStatus() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace bedrock::archive::transcriber
