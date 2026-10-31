#include <platform/os_io.h>

#include <algorithm>
#if defined(BEDROCK_OS_IO_TESTING)
#include <atomic>
#endif
#include <array>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <limits>
#include <string>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/statfs.h>
#include <unistd.h>
#if defined(__linux__)
#include <linux/falloc.h>
#endif
#endif

namespace bedrock::internal::os {
namespace {
IoStatus Status(IoError error, std::uint32_t native = 0) noexcept { return {error, native}; }
#if defined(BEDROCK_OS_IO_TESTING)
std::atomic<PendingLockObserver> pending_lock_observer{nullptr};
#endif
#if defined(_WIN32)
IoStatus WinError(DWORD e) noexcept {
  switch (e) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: return Status(IoError::kNotFound, e);
    case ERROR_ACCESS_DENIED: case ERROR_SHARING_VIOLATION: return Status(IoError::kAccessDenied, e);
    case ERROR_LOCK_VIOLATION: case ERROR_IO_PENDING: return Status(IoError::kBusy, e);
    case ERROR_INVALID_PARAMETER: case ERROR_INVALID_NAME: return Status(IoError::kInvalidArgument, e);
    default: return Status(IoError::kSystemError, e);
  }
}
bool PathToWide(std::string_view s, wchar_t* out, int capacity) noexcept {
  if (s.empty() || s.find('\0') != std::string_view::npos || s.size() > INT_MAX) return false;
  const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out, capacity - 1);
  if (n <= 0 || n >= capacity) return false;
  out[n] = L'\0'; return true;
}
#else
IoStatus PosixError(int e) noexcept {
  switch (e) {
    case ENOENT: case ENOTDIR: return Status(IoError::kNotFound, static_cast<std::uint32_t>(e));
    case EACCES: case EPERM: return Status(IoError::kAccessDenied, static_cast<std::uint32_t>(e));
    case EINVAL: case EOVERFLOW: return Status(IoError::kInvalidArgument, static_cast<std::uint32_t>(e));
    case EEXIST: case EAGAIN: return Status(IoError::kBusy, static_cast<std::uint32_t>(e));
    default: return Status(IoError::kSystemError, static_cast<std::uint32_t>(e));
  }
}
#endif
bool ValidRange(std::uint64_t offset, std::uint64_t length) noexcept {
  return length != 0 && offset <= static_cast<std::uint64_t>(INT64_MAX) &&
         length <= static_cast<std::uint64_t>(INT64_MAX) - offset;
}
}  // namespace

File::~File() { Close(); }
File::File(File&& other) noexcept : native_(std::exchange(other.native_, 0)) {}
File& File::operator=(File&& other) noexcept {
  if (this != &other) { Close(); native_ = std::exchange(other.native_, 0); }
  return *this;
}
bool File::IsOpen() const noexcept {
#if defined(_WIN32)
  return native_ != 0 && reinterpret_cast<HANDLE>(native_) != INVALID_HANDLE_VALUE;
#else
  return native_ != 0;
#endif
}
void File::Close() noexcept {
  if (!IsOpen()) { native_ = 0; return; }
#if defined(_WIN32)
  CloseHandle(reinterpret_cast<HANDLE>(native_));
#else
  ::close(static_cast<int>(native_ - 1));
#endif
  native_ = 0;
}

IoStatus ReadEnvironment(std::string_view name, std::span<char> output, std::size_t& bytes) noexcept {
  bytes = 0;
  if (name.empty() || name.find('\0') != std::string_view::npos) return Status(IoError::kInvalidArgument);
#if defined(_WIN32)
  wchar_t wide_name[32768];
  if (!PathToWide(name, wide_name, static_cast<int>(std::size(wide_name)))) return Status(IoError::kInvalidArgument);
  SetLastError(ERROR_SUCCESS);
  DWORD required = GetEnvironmentVariableW(wide_name, nullptr, 0);
  const DWORD first_error = required == 0 ? GetLastError() : ERROR_SUCCESS;
  bool present_empty = required == 0 && first_error == ERROR_SUCCESS;
  if (!required) {
    if (first_error == ERROR_ENVVAR_NOT_FOUND) {
      // GetEnvironmentVariableW may report an empty value identically to an
      // absent name on some Windows configurations. Enumerate the block to
      // distinguish NAME= from a missing NAME.
      LPWCH block = GetEnvironmentStringsW();
      if (!block) return Status(IoError::kSystemError, GetLastError());
      const std::size_t name_len = std::wcslen(wide_name);
      bool found_empty = false;
      for (const wchar_t* item = block; *item; item += std::wcslen(item) + 1) {
        if (std::wcslen(item) == name_len &&
            CompareStringOrdinal(item, static_cast<int>(name_len), wide_name,
                                 static_cast<int>(name_len), TRUE) == CSTR_EQUAL) {
          found_empty = true;
          break;
        }
        if (std::wcslen(item) > name_len && item[name_len] == L'=' &&
            CompareStringOrdinal(item, static_cast<int>(name_len), wide_name,
                                 static_cast<int>(name_len), TRUE) == CSTR_EQUAL) {
          found_empty = item[name_len + 1] == L'\0';
          break;
        }
      }
      FreeEnvironmentStringsW(block);
      if (!found_empty) return Status(IoError::kNotFound, first_error);
      required = 1;
      present_empty = true;
    } else if (first_error != ERROR_SUCCESS) return WinError(first_error);
    else required = 1; // Present but empty.
  }
  const std::size_t wide_count = required;
  wchar_t* wide = static_cast<wchar_t*>(LocalAlloc(LMEM_FIXED, wide_count * sizeof(wchar_t)));
  if (!wide) return Status(IoError::kSystemError, ERROR_NOT_ENOUGH_MEMORY);
  DWORD got = 0;
  if (present_empty) { wide[0] = L'\0'; }
  else {
    SetLastError(ERROR_SUCCESS);
    got = GetEnvironmentVariableW(wide_name, wide, required);
    if (!got) {
      const DWORD e = GetLastError();
      if (e == ERROR_ENVVAR_NOT_FOUND) { LocalFree(wide); return Status(IoError::kNotFound, e); }
      if (e != ERROR_SUCCESS) { LocalFree(wide); return WinError(e); }
      // A value can become empty between the size query and read.
      present_empty = true;
      wide[0] = L'\0';
    } else if (got >= required) {
      // The value grew after the size query. Never convert beyond our allocation.
      LocalFree(wide);
      return Status(IoError::kBufferTooSmall);
    }
  }
  if (present_empty) got = 0;
  const int need = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, static_cast<int>(got), nullptr, 0, nullptr, nullptr);
  if (got != 0 && need == 0) { const DWORD e = GetLastError(); LocalFree(wide); return WinError(e); }
  bytes = static_cast<std::size_t>(need);
  if (bytes > output.size()) { LocalFree(wide); return Status(IoError::kBufferTooSmall); }
  if (need && !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, static_cast<int>(got), output.data(), need, nullptr, nullptr)) {
    const DWORD e = GetLastError(); LocalFree(wide); return WinError(e);
  }
  LocalFree(wide); return {};
#else
  std::array<char, 32768> key{};
  if (name.size() >= key.size()) return Status(IoError::kInvalidArgument);
  std::memcpy(key.data(), name.data(), name.size());
  const char* value = std::getenv(key.data());
  if (!value) return Status(IoError::kNotFound);
  const std::size_t n = std::strlen(value);
  bytes = n;
  if (n > output.size()) return Status(IoError::kBufferTooSmall);
  if (n) std::memcpy(output.data(), value, n);
  return {};
#endif
}

IoStatus SharedDataDirectory(std::span<char> output, std::size_t& bytes) noexcept {
  bytes = 0;
#if defined(_WIN32)
  PWSTR folder = nullptr;
  const HRESULT hr = SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_DEFAULT, nullptr, &folder);
  if (FAILED(hr)) return Status(IoError::kSystemError, static_cast<std::uint32_t>(hr));
  const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, folder, -1, nullptr, 0, nullptr, nullptr);
  if (required <= 0) { const DWORD e = GetLastError(); CoTaskMemFree(folder); return WinError(e); }
  bytes = static_cast<std::size_t>(required - 1);
  if (bytes > output.size()) { CoTaskMemFree(folder); return Status(IoError::kBufferTooSmall); }
  if (bytes && !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, folder, required - 1, output.data(), static_cast<int>(bytes), nullptr, nullptr)) {
    const DWORD e = GetLastError(); CoTaskMemFree(folder); return WinError(e);
  }
  CoTaskMemFree(folder); return {};
#elif defined(__linux__)
  constexpr std::string_view path = "/var/lib";
  bytes = path.size();
  if (bytes > output.size()) return Status(IoError::kBufferTooSmall);
  std::memcpy(output.data(), path.data(), bytes); return {};
#else
  (void)output; return Status(IoError::kUnsupported);
#endif
}

IoStatus ValidateLocalDirectory(std::string_view path) noexcept {
  if (path.empty() || path.find('\0') != std::string_view::npos) return Status(IoError::kInvalidArgument);
#if defined(_WIN32)
  wchar_t wide[32768];
  if (!PathToWide(path, wide, static_cast<int>(std::size(wide)))) return Status(IoError::kInvalidArgument);
  const bool absolute_drive = std::wcslen(wide) >= 3 && ((wide[0] >= L'A' && wide[0] <= L'Z') || (wide[0] >= L'a' && wide[0] <= L'z')) && wide[1] == L':' && (wide[2] == L'\\' || wide[2] == L'/');
  if (!absolute_drive || (wide[0] == L'\\' && wide[1] == L'\\')) return Status(IoError::kInvalidArgument);
  wchar_t root[4]{wide[0], L':', L'\\', L'\0'};
  if (GetDriveTypeW(root) != DRIVE_FIXED && GetDriveTypeW(root) != DRIVE_REMOVABLE) return Status(IoError::kUnsupported);
  HANDLE h = CreateFileW(wide, FILE_LIST_DIRECTORY | GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (h == INVALID_HANDLE_VALUE) return WinError(GetLastError());
  BY_HANDLE_FILE_INFORMATION info{};
  wchar_t resolved[32768]{};
  const DWORD resolved_len = GetFinalPathNameByHandleW(h, resolved, static_cast<DWORD>(std::size(resolved)), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  const bool dir = GetFileInformationByHandle(h, &info) && (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
  CloseHandle(h);
  if (!dir || resolved_len == 0 || resolved_len >= std::size(resolved)) return Status(IoError::kInvalidArgument);
  // Resolve junctions/symlinks before determining the backing volume as well.
  wchar_t resolved_root[7]{};
  if (resolved_len < 7 || resolved[0] != L'\\' || resolved[1] != L'\\' || resolved[2] != L'?' || resolved[3] != L'\\' ||
      !((resolved[4] >= L'A' && resolved[4] <= L'Z') || (resolved[4] >= L'a' && resolved[4] <= L'z')) ||
      resolved[5] != L':' || resolved[6] != L'\\') return Status(IoError::kUnsupported);
  resolved_root[0] = resolved[4]; resolved_root[1] = L':'; resolved_root[2] = L'\\';
  const UINT type = GetDriveTypeW(resolved_root);
  return type == DRIVE_FIXED || type == DRIVE_REMOVABLE ? IoStatus{} : Status(IoError::kUnsupported);
#elif defined(__linux__)
  if (path.front() != '/' || path.size() > PATH_MAX) return Status(IoError::kInvalidArgument);
  char terminated[PATH_MAX + 1]; std::memcpy(terminated, path.data(), path.size()); terminated[path.size()] = '\0';
  struct stat st{};
  if (::stat(terminated, &st) != 0) return PosixError(errno);
  if (!S_ISDIR(st.st_mode)) return Status(IoError::kInvalidArgument);
  struct statfs fs{};
  if (::statfs(terminated, &fs) != 0) return PosixError(errno);
  // Linux network/cluster filesystem magic values (NFS, CIFS, SMB, Ceph, etc.).
  switch (static_cast<unsigned long>(fs.f_type)) {
    case 0x6969UL: case 0xFF534D42UL: case 0xFE534D42UL: case 0x517BUL:
    case 0x73757245UL: case 0x5346414FUL: case 0x00C36400UL:
    case 0x01021997UL: case 0x0BD00BD0UL: return Status(IoError::kUnsupported);
    default: break;
  }
  if (::access(terminated, R_OK | W_OK | X_OK) != 0) return PosixError(errno);
  return {};
#else
  (void)path; return Status(IoError::kUnsupported);
#endif
}

OpenResult Open(std::string_view path, OpenMode mode, File& out) noexcept {
  if (out.IsOpen()) return {Status(IoError::kInvalidArgument), false};
  if (path.empty() || path.find('\0') != std::string_view::npos) return {Status(IoError::kInvalidArgument), false};
#if defined(_WIN32)
  wchar_t wide[32768];
  if (!PathToWide(path, wide, static_cast<int>(std::size(wide)))) return {Status(IoError::kInvalidArgument), false};
  bool created = false;
  HANDLE h = INVALID_HANDLE_VALUE;
  if (mode == OpenMode::kReadOnly) {
    h = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
  } else {
    h = CreateFileW(wide, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    if (h != INVALID_HANDLE_VALUE) created = true;
    else {
      const DWORD creation_error = GetLastError();
      if (creation_error == ERROR_FILE_EXISTS || creation_error == ERROR_ALREADY_EXISTS) {
        h = CreateFileW(wide, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
      }
    }
  }
  if (h == INVALID_HANDLE_VALUE) return {WinError(GetLastError()), false};
  out.native_ = reinterpret_cast<std::uintptr_t>(h);
  return {{}, created};
#elif defined(__linux__)
  if (path.size() > PATH_MAX) return {Status(IoError::kInvalidArgument), false};
  char terminated[PATH_MAX + 1]; std::memcpy(terminated, path.data(), path.size()); terminated[path.size()] = '\0';
  bool created = false; int fd;
  const int flags = (mode == OpenMode::kReadOnly ? O_RDONLY : O_RDWR) | O_CLOEXEC;
  if (mode == OpenMode::kReadOnly) fd = ::open(terminated, flags);
  else {
    fd = ::open(terminated, flags | O_CREAT | O_EXCL, 0666);
    if (fd >= 0) created = true;
    else if (errno == EEXIST) fd = ::open(terminated, flags);
  }
  if (fd < 0) return {PosixError(errno), false};
  out.native_ = static_cast<std::uintptr_t>(fd) + 1;
  return {{}, created};
#else
  (void)mode; return {Status(IoError::kUnsupported), false};
#endif
}

IoStatus FileSize(const File& file, std::uint64_t& bytes) noexcept {
  bytes = 0; if (!file.IsOpen()) return Status(IoError::kInvalidArgument);
#if defined(_WIN32)
  LARGE_INTEGER size{}; if (!GetFileSizeEx(reinterpret_cast<HANDLE>(file.native_), &size)) return WinError(GetLastError());
  if (size.QuadPart < 0) return Status(IoError::kSystemError); bytes = static_cast<std::uint64_t>(size.QuadPart); return {};
#elif defined(__linux__)
  struct stat st{}; if (fstat(static_cast<int>(file.native_ - 1), &st) != 0) return PosixError(errno);
  if (st.st_size < 0) return Status(IoError::kSystemError);
  bytes = static_cast<std::uint64_t>(st.st_size);
  return {};
#else
  return Status(IoError::kUnsupported);
#endif
}

IoStatus ReadAt(const File& file, std::uint64_t offset, std::span<std::byte> output, std::size_t& bytes) noexcept {
  bytes = 0;
  if (!file.IsOpen() || offset > static_cast<std::uint64_t>(INT64_MAX) ||
      output.size() > static_cast<std::uint64_t>(INT64_MAX) - offset)
    return Status(IoError::kInvalidArgument);
#if defined(_WIN32)
  if (output.size() > MAXDWORD) return Status(IoError::kInvalidArgument);
  OVERLAPPED ov{}; ov.Offset = static_cast<DWORD>(offset); ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
  DWORD got = 0;
  BOOL ok = ReadFile(reinterpret_cast<HANDLE>(file.native_), output.data(), static_cast<DWORD>(output.size()), &got, &ov);
  if (!ok && GetLastError() == ERROR_IO_PENDING) ok = GetOverlappedResult(reinterpret_cast<HANDLE>(file.native_), &ov, &got, TRUE);
  if (!ok) { const DWORD e = GetLastError(); if (e != ERROR_HANDLE_EOF) return WinError(e); }
  bytes = got; return {};
#elif defined(__linux__)
  ssize_t n; do { n = pread(static_cast<int>(file.native_ - 1), output.data(), output.size(), static_cast<off_t>(offset)); } while (n < 0 && errno == EINTR);
  if (n < 0) return PosixError(errno);
  bytes = static_cast<std::size_t>(n);
  return {};
#else
  (void)output; return Status(IoError::kUnsupported);
#endif
}
IoStatus WriteAt(const File& file, std::uint64_t offset, std::span<const std::byte> input, std::size_t& bytes) noexcept {
  bytes = 0;
  if (!file.IsOpen() || offset > static_cast<std::uint64_t>(INT64_MAX) ||
      input.size() > static_cast<std::uint64_t>(INT64_MAX) - offset)
    return Status(IoError::kInvalidArgument);
#if defined(_WIN32)
  if (input.size() > MAXDWORD) return Status(IoError::kInvalidArgument);
  OVERLAPPED ov{}; ov.Offset = static_cast<DWORD>(offset); ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
  DWORD put = 0;
  BOOL ok = WriteFile(reinterpret_cast<HANDLE>(file.native_), input.data(), static_cast<DWORD>(input.size()), &put, &ov);
  if (!ok && GetLastError() == ERROR_IO_PENDING) ok = GetOverlappedResult(reinterpret_cast<HANDLE>(file.native_), &ov, &put, TRUE);
  if (!ok) return WinError(GetLastError());
  bytes = put; return {};
#elif defined(__linux__)
  ssize_t n; do { n = pwrite(static_cast<int>(file.native_ - 1), input.data(), input.size(), static_cast<off_t>(offset)); } while (n < 0 && errno == EINTR);
  if (n < 0) return PosixError(errno);
  bytes = static_cast<std::size_t>(n);
  return {};
#else
  (void)input; return Status(IoError::kUnsupported);
#endif
}
IoStatus Flush(const File& file) noexcept {
  if (!file.IsOpen()) return Status(IoError::kInvalidArgument);
#if defined(_WIN32)
  if (!FlushFileBuffers(reinterpret_cast<HANDLE>(file.native_))) return WinError(GetLastError()); return {};
#elif defined(__linux__)
  int rc; do { rc = fsync(static_cast<int>(file.native_ - 1)); } while (rc != 0 && errno == EINTR);
  return rc == 0 ? IoStatus{} : PosixError(errno);
#else
  return Status(IoError::kUnsupported);
#endif
}
 #if defined(BEDROCK_OS_IO_TESTING)
void SetPendingLockObserverForTesting(PendingLockObserver observer) noexcept {
  pending_lock_observer.store(observer, std::memory_order_release);
}
#endif

IoStatus LockRange(const File& file, std::uint64_t offset, std::uint64_t length, bool wait) noexcept {
  if (!file.IsOpen() || !ValidRange(offset, length)) return Status(IoError::kInvalidArgument);
#if defined(_WIN32)
  OVERLAPPED ov{}; ov.Offset = static_cast<DWORD>(offset); ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
  DWORD flags = LOCKFILE_EXCLUSIVE_LOCK | (wait ? 0 : LOCKFILE_FAIL_IMMEDIATELY);
  if (!LockFileEx(reinterpret_cast<HANDLE>(file.native_), flags, 0, static_cast<DWORD>(length), static_cast<DWORD>(length >> 32), &ov)) {
    if (wait && GetLastError() == ERROR_IO_PENDING) {
#if defined(BEDROCK_OS_IO_TESTING)
      if (const auto observer = pending_lock_observer.load(std::memory_order_acquire)) observer();
#endif
      DWORD transferred = 0;
      if (GetOverlappedResult(reinterpret_cast<HANDLE>(file.native_), &ov, &transferred, TRUE)) return {};
    }
    return WinError(GetLastError());
  }
  return {};
#elif defined(__linux__) && defined(F_OFD_SETLK) && defined(F_OFD_SETLKW)
  struct flock lock{}; lock.l_type = F_WRLCK; lock.l_whence = SEEK_SET;
  lock.l_start = static_cast<off_t>(offset); lock.l_len = static_cast<off_t>(length);
  int rc; do { rc = fcntl(static_cast<int>(file.native_ - 1), wait ? F_OFD_SETLKW : F_OFD_SETLK, &lock); } while (rc < 0 && errno == EINTR && wait);
  if (rc == 0) return {};
  if (errno == EAGAIN || errno == EACCES) return Status(IoError::kBusy, static_cast<std::uint32_t>(errno));
  return PosixError(errno);
#else
  (void)wait; return Status(IoError::kUnsupported);
#endif
}
IoStatus UnlockRange(const File& file, std::uint64_t offset, std::uint64_t length) noexcept {
  if (!file.IsOpen() || !ValidRange(offset, length)) return Status(IoError::kInvalidArgument);
#if defined(_WIN32)
  OVERLAPPED ov{}; ov.Offset = static_cast<DWORD>(offset); ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
  if (!UnlockFileEx(reinterpret_cast<HANDLE>(file.native_), 0, static_cast<DWORD>(length), static_cast<DWORD>(length >> 32), &ov)) return WinError(GetLastError()); return {};
#elif defined(__linux__) && defined(F_OFD_SETLK)
  struct flock lock{}; lock.l_type = F_UNLCK; lock.l_whence = SEEK_SET; lock.l_start = static_cast<off_t>(offset); lock.l_len = static_cast<off_t>(length);
  if (fcntl(static_cast<int>(file.native_ - 1), F_OFD_SETLK, &lock) != 0) return PosixError(errno);
  return {};
#else
  return Status(IoError::kUnsupported);
#endif
}
}  // namespace bedrock::internal::os
