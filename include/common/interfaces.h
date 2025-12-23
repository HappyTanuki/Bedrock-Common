#ifndef BEDROCK_COMMON_COMMON_INTERFACES_H_
#define BEDROCK_COMMON_COMMON_INTERFACES_H_

#include <array>
#include <cstdint>
#include <span>

#include "types_enums.h"

namespace bedrock {

class Validatable {
 public:
  virtual ~Validatable() = default;

  virtual bool IsValid() const = 0;
};

template <std::uint32_t BufferSize, typename StatusType>
  requires StatusEnum<StatusType>
class ReadWritable {
 public:
  virtual ~ReadWritable() = default;

  virtual DataWithStatus<std::array<std::byte, BufferSize>, StatusType>
  Read() = 0;
  virtual StatusType Write(std::span<std::byte> data) = 0;
};

}  // namespace bedrock

#endif