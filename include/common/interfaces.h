#ifndef BEDROCK_COMMON_COMMON_INTERFACES_H_
#define BEDROCK_COMMON_COMMON_INTERFACES_H_

#include <cstdint>
#include <span>
#include <vector>
#include <optional>

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

  virtual ~ReadWritable() = default;

  virtual DataWithStatus<std::pair<std::vector<std::byte>, std::uint32_t>,
                         StatusType>
  Read(std::uint32_t request_size) = 0;
  virtual StatusType Write(std::span<const std::byte> data) = 0;
};

// CRTP helper for failable construction via a static implementation hook.
// The derived class must:
// 1) Declare `ConstructFailable<Derived>` as a friend to allow access to
//    its non-public static `CreateImpl(...)`.
// 2) Provide a static `CreateImpl(Args...)` that returns
//    `std::optional<Derived>` (or compatible).
// 3) Make its constructors non-public (private/protected) to prevent
//    direct construction and enforce creation via `Create(...)`.
template <typename ClassType>
class ConstructFailable {
 public:
  template <typename... Args>
  static std::optional<ClassType> Create(Args&&... args) {
    return ClassType::CreateImpl(std::forward<Args>(args)...);
  }
};

}  // namespace bedrock

#endif
