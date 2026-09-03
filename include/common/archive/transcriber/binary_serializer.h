/** @file binary_serializer.h @brief Public binary serializer template. */
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
 * @brief Public API boundary for a format-specific binary serializer.
 *
 * The pImpl keeps the format implementation, output buffer, and diagnostic
 * storage owned by the library. Owning STL objects and internal representation
 * types therefore do not cross the DLL boundary. Output() returns a borrowed
 * span invalidated by Reset(), move assignment, or destruction.
 */
template <typename Format>
class BinarySerializer {
 public:
  explicit BinarySerializer(std::uint16_t machine_id = 0);
  ~BinarySerializer();
  BinarySerializer(const BinarySerializer&) = delete;
  BinarySerializer& operator=(const BinarySerializer&) = delete;
  BinarySerializer(BinarySerializer&&) noexcept;
  BinarySerializer& operator=(BinarySerializer&&) noexcept;

  void Reset() &;
  [[nodiscard]] bedrock::Status Dump(bedrock::archive::Schema& schema,
                                     std::string_view name = "") &;
  bedrock::Status Dump(bedrock::archive::Schema&,
                       std::string_view = "") && = delete;
  [[nodiscard]] std::span<const std::byte> Output() const noexcept;
  [[nodiscard]] bedrock::Status GetStatus() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace bedrock::archive::transcriber
