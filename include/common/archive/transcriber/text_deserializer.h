/** @file text_deserializer.h @brief Public text deserializer template. */
#pragma once
#include <cstdint>
#include <memory>
#include <string_view>

#include "common/archive.h"
#include "common/data_types/status.h"

namespace bedrock::archive::transcriber {

/**
 * @brief Public API boundary for a format-specific text deserializer.
 *
 * The pImpl keeps the format implementation, copied input, parsed
 * representation, and diagnostic storage owned by the library. Owning STL
 * objects and internal representation types therefore do not cross the DLL
 * boundary. Initialize() copies the supplied view during the call.
 */
template <typename Format>
class TextDeserializer {
 public:
  explicit TextDeserializer(std::uint16_t machine_id = 0);
  TextDeserializer(std::uint16_t machine_id, std::string_view input);
  ~TextDeserializer();
  TextDeserializer(const TextDeserializer&) = delete;
  TextDeserializer& operator=(const TextDeserializer&) = delete;
  TextDeserializer(TextDeserializer&&) noexcept;
  TextDeserializer& operator=(TextDeserializer&&) noexcept;

  [[nodiscard]] bedrock::Status Initialize(std::string_view input) &;
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
