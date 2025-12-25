#ifndef BEDROCK_COMMON_COMMON_INTERFACES_H_
#define BEDROCK_COMMON_COMMON_INTERFACES_H_

#include <cstdint>
#include <span>
#include <vector>

#include "types_enums.h"

namespace bedrock {

class Validatable {
 public:
  Validatable() = default;
  Validatable(const Validatable&) = default;
  Validatable& operator=(const Validatable&) = default;
  virtual ~Validatable();

  virtual bool IsValid() const = 0;
};

template <typename StatusType>
  requires StatusEnum<StatusType>
class ReadWritable {
 public:
  ReadWritable() = default;
  ReadWritable(const ReadWritable&) = default;
  ReadWritable& operator=(const ReadWritable&) = default;

  virtual ~ReadWritable();

  virtual DataWithStatus<std::pair<std::vector<std::byte>, std::uint32_t>,
                         StatusType>
  Read(std::uint32_t request_size) = 0;
  virtual StatusType Write(std::span<const std::byte> data) = 0;
};

}  // namespace bedrock

template <typename StatusType>
  requires bedrock::StatusEnum<StatusType>
bedrock::ReadWritable<StatusType>::~ReadWritable() = default;

#endif
