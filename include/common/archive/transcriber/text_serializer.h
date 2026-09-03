/** @file text_serializer.h @brief Public text serializer template. */
#pragma once
#include <cstdint>
#include <memory>
#include <string_view>

#include "common/archive.h"
#include "common/data_types/status.h"

namespace bedrock::archive::transcriber {

/**
 * @brief Public API boundary for a format-specific text serializer.
 *
 * The pImpl keeps the format implementation, output buffer, and diagnostic
 * storage owned by the library. Owning STL objects and internal representation
 * types therefore do not cross the DLL boundary. Output() returns a borrowed
 * view invalidated by Reset(), move assignment, or destruction.
 */
template <typename Format>
class TextSerializer {
 public:
  explicit TextSerializer(std::uint16_t machine_id = 0);
  ~TextSerializer();
  TextSerializer(const TextSerializer&) = delete;
  TextSerializer& operator=(const TextSerializer&) = delete;
  TextSerializer(TextSerializer&&) noexcept;
  TextSerializer& operator=(TextSerializer&&) noexcept;

  void Reset() &;
  [[nodiscard]] bedrock::Status Dump(bedrock::archive::Schema& schema,
                                     std::string_view name = "") &;
  bedrock::Status Dump(bedrock::archive::Schema&,
                       std::string_view = "") && = delete;
  [[nodiscard]] std::string_view Output() const noexcept;
  [[nodiscard]] bedrock::Status GetStatus() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace bedrock::archive::transcriber
