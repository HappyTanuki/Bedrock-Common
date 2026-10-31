#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace bedrock::internal::os {

#if defined(BEDROCK_OS_IO_TESTING)
using PendingLockObserver = void (*)() noexcept;
void SetPendingLockObserverForTesting(PendingLockObserver observer) noexcept;
#endif

enum class IoError : std::uint8_t {
  kOk, kNotFound, kAccessDenied, kInvalidArgument, kBufferTooSmall,
  kBusy, kUnsupported, kSystemError
};
struct IoStatus {
  IoError code = IoError::kOk;
  std::uint32_t native_error = 0;
  [[nodiscard]] bool Ok() const noexcept { return code == IoError::kOk; }
};
enum class OpenMode : std::uint8_t { kReadOnly, kReadWriteCreate };
struct OpenResult { IoStatus status; bool created = false; };

class File final {
 public:
  File() noexcept = default;
  ~File();
  File(File&& other) noexcept;
  File& operator=(File&& other) noexcept;
  File(const File&) = delete;
  File& operator=(const File&) = delete;
  [[nodiscard]] bool IsOpen() const noexcept;
  void Close() noexcept;
 private:
  std::uintptr_t native_ = 0;  // fd+1 on Linux; HANDLE on Windows
  friend OpenResult Open(std::string_view, OpenMode, File&) noexcept;
  friend IoStatus FileSize(const File&, std::uint64_t&) noexcept;
  friend IoStatus ReadAt(const File&, std::uint64_t, std::span<std::byte>, std::size_t&) noexcept;
  friend IoStatus WriteAt(const File&, std::uint64_t, std::span<const std::byte>, std::size_t&) noexcept;
  friend IoStatus Flush(const File&) noexcept;
  friend IoStatus LockRange(const File&, std::uint64_t, std::uint64_t, bool) noexcept;
  friend IoStatus UnlockRange(const File&, std::uint64_t, std::uint64_t) noexcept;
};

IoStatus ReadEnvironment(std::string_view name, std::span<char> output,
                         std::size_t& bytes) noexcept;
IoStatus SharedDataDirectory(std::span<char> output,
                             std::size_t& bytes) noexcept;
/**
 * Validate an existing absolute local directory with read/write access.
 * kInvalidArgument: empty, embedded NUL, not absolute (on Windows anything but
 * a drive-letter path, including UNC and drive-relative forms), invalid UTF-8
 * (Windows) or not a directory. kNotFound: missing. kUnsupported: a network or
 * otherwise non-local volume, or an unsupported platform. Other failures carry
 * the native error. Validation never creates or modifies the path.
 */
IoStatus ValidateLocalDirectory(std::string_view utf8_path) noexcept;
OpenResult Open(std::string_view utf8_path, OpenMode mode, File& out) noexcept;
IoStatus FileSize(const File&, std::uint64_t& bytes) noexcept;
IoStatus ReadAt(const File&, std::uint64_t offset,
                std::span<std::byte> output, std::size_t& bytes) noexcept;
IoStatus WriteAt(const File&, std::uint64_t offset,
                 std::span<const std::byte> input, std::size_t& bytes) noexcept;
IoStatus Flush(const File&) noexcept;
IoStatus LockRange(const File&, std::uint64_t offset,
                   std::uint64_t length, bool wait) noexcept;
IoStatus UnlockRange(const File&, std::uint64_t offset,
                     std::uint64_t length) noexcept;

}  // namespace bedrock::internal::os
