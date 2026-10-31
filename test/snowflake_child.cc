#include "snowflake_child.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace bedrock::test {
namespace {

std::atomic<std::uint64_t> launch_attempts{0};

std::string ErrorText(const std::error_code& ec) {
  return std::to_string(ec.value()) + " (" + ec.message() + ")";
}

bool HasNul(std::string_view text) noexcept {
  return text.find('\0') != std::string_view::npos;
}

bool ValidChildText(std::string_view what, std::string_view text,
                    std::string& diagnostic) {
  if (HasNul(text)) {
    diagnostic = std::string(what) + " contains NUL";
    return false;
  }
  if (!IsValidUtf8(text)) {
    diagnostic = std::string(what) + " is not valid UTF-8";
    return false;
  }
  return true;
}

#if !defined(_WIN32)
void Sleep1ms() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
#endif

#if defined(_WIN32)
bool SameNameIgnoreCase(std::wstring_view a, std::wstring_view b) noexcept {
  return a.size() == b.size() &&
         CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                              static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

std::wstring_view EntryName(std::wstring_view entry) noexcept {
  // Hidden per-drive entries such as "=C:=C:\dir" start with '='.
  const std::size_t equals = entry.find(L'=', 1);
  return equals == std::wstring_view::npos ? entry : entry.substr(0, equals);
}

void AppendQuoted(std::wstring& command, std::wstring_view argument) {
  // Inverse of CommandLineToArgvW / MSVC CRT argument parsing.
  command.push_back(L' ');
  command.push_back(L'"');
  for (std::size_t i = 0;; ++i) {
    std::size_t backslashes = 0;
    while (i < argument.size() && argument[i] == L'\\') {
      ++backslashes;
      ++i;
    }
    if (i == argument.size()) {
      command.append(backslashes * 2, L'\\');
      break;
    }
    if (argument[i] == L'"') {
      command.append(backslashes * 2 + 1, L'\\');
    } else {
      command.append(backslashes, L'\\');
    }
    command.push_back(argument[i]);
  }
  command.push_back(L'"');
}

bool EnvironmentEntries(std::vector<std::wstring>& entries,
                        std::string& diagnostic) {
  LPWCH block = GetEnvironmentStringsW();
  if (block == nullptr) {
    diagnostic = "GetEnvironmentStringsW failed: " + std::to_string(GetLastError());
    return false;
  }
  for (const wchar_t* item = block; *item != L'\0'; item += std::wcslen(item) + 1)
    entries.emplace_back(item);
  FreeEnvironmentStringsW(block);
  return true;
}
#endif

}  // namespace

bool IsValidUtf8(std::string_view text) noexcept {
  std::size_t i = 0;
  while (i < text.size()) {
    const auto lead = static_cast<unsigned char>(text[i]);
    if (lead < 0x80) {
      ++i;
      continue;
    }
    std::size_t trail = 0;
    std::uint32_t code = 0;
    if (lead >= 0xC2 && lead <= 0xDF) {
      trail = 1;
      code = lead & 0x1Fu;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
      trail = 2;
      code = lead & 0x0Fu;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
      trail = 3;
      code = lead & 0x07u;
    } else {
      return false;
    }
    if (text.size() - i <= trail) return false;
    for (std::size_t k = 1; k <= trail; ++k) {
      const auto next = static_cast<unsigned char>(text[i + k]);
      if ((next & 0xC0u) != 0x80u) return false;
      code = (code << 6) | (next & 0x3Fu);
    }
    if (trail == 2 && (code < 0x800u || (code >= 0xD800u && code <= 0xDFFFu)))
      return false;
    if (trail == 3 && (code < 0x10000u || code > 0x10FFFFu)) return false;
    i += trail + 1;
  }
  return true;
}

#if defined(_WIN32)
bool Utf8ToWide(std::string_view utf8, std::wstring& wide) {
  wide.clear();
  if (utf8.empty()) return true;  // Empty is a value, not a conversion failure.
  if (utf8.size() > static_cast<std::size_t>(INT_MAX)) return false;
  const int input = static_cast<int>(utf8.size());
  const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         utf8.data(), input, nullptr, 0);
  if (needed <= 0) return false;
  std::wstring converted(static_cast<std::size_t>(needed), L'\0');
  const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                          utf8.data(), input, converted.data(),
                                          needed);
  if (written != needed) return false;
  wide = std::move(converted);
  return true;
}

bool WideToUtf8(std::wstring_view wide, std::string& utf8) {
  utf8.clear();
  if (wide.empty()) return true;
  if (wide.size() > static_cast<std::size_t>(INT_MAX)) return false;
  const int input = static_cast<int>(wide.size());
  const int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
                                         input, nullptr, 0, nullptr, nullptr);
  if (needed <= 0) return false;
  std::string converted(static_cast<std::size_t>(needed), '\0');
  const int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                          wide.data(), input, converted.data(),
                                          needed, nullptr, nullptr);
  if (written != needed) return false;
  utf8 = std::move(converted);
  return true;
}
#endif

std::string PathToUtf8(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::filesystem::path PathFromUtf8(std::string_view utf8) {
  return std::filesystem::path(std::u8string(
      reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

bool CurrentExecutablePath(std::filesystem::path& path,
                           std::string& diagnostic) noexcept {
#if defined(_WIN32)
  std::wstring buffer(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                          static_cast<DWORD>(buffer.size()));
  if (length == 0 || length >= buffer.size()) {
    diagnostic = "GetModuleFileNameW failed: " + std::to_string(GetLastError());
    return false;
  }
  buffer.resize(length);
  path = std::filesystem::path(buffer);
  return true;
#else
  std::error_code ec;
  path = std::filesystem::read_symlink("/proc/self/exe", ec);
  if (ec || path.empty()) {
    diagnostic = "readlink(/proc/self/exe) failed: " + ErrorText(ec);
    return false;
  }
  return true;
#endif
}

bool NativeHelperArguments(int argc, char** argv, std::vector<std::string>& args,
                           std::string& diagnostic) {
  args.clear();
#if defined(_WIN32)
  (void)argv;
  int count = 0;
  LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
  if (wide == nullptr) {
    diagnostic = "CommandLineToArgvW failed: " + std::to_string(GetLastError());
    return false;
  }
  bool ok = count == argc;
  if (!ok) diagnostic = "native argument count differs from argc";
  for (int i = 0; ok && i < count; ++i) {
    std::string utf8;
    if (!WideToUtf8(wide[i], utf8)) {
      diagnostic = "native argument " + std::to_string(i) + " is not valid UTF-16";
      ok = false;
      break;
    }
    args.push_back(std::move(utf8));
  }
  LocalFree(wide);
  return ok;
#else
  for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
  (void)diagnostic;
  return true;
#endif
}

bool ReadProcessEnvironment(std::string_view name, bool& present,
                            std::string& value) {
  present = false;
  value.clear();
#if defined(_WIN32)
  std::wstring wide_name;
  if (!Utf8ToWide(name, wide_name) || wide_name.empty()) return false;
  // Enumerate the block: GetEnvironmentVariableW can report an empty value
  // like an absent one.
  std::vector<std::wstring> entries;
  std::string ignored;
  if (!EnvironmentEntries(entries, ignored)) return false;
  for (const auto& entry : entries) {
    const std::wstring_view view(entry);
    if (!SameNameIgnoreCase(EntryName(view), wide_name)) continue;
    present = true;
    const std::size_t equals = view.find(L'=', 1);
    if (equals == std::wstring_view::npos) return true;
    return WideToUtf8(view.substr(equals + 1), value);
  }
  return true;
#else
  const std::string key(name);
  const char* text = std::getenv(key.c_str());
  if (text == nullptr) return true;
  present = true;
  value = text;
  return true;
#endif
}

bool WriteProcessEnvironment(std::string_view name,
                             const ChildEnvironmentValue& value) {
  if (HasNul(name) || (value.present && HasNul(value.value))) return false;
#if defined(_WIN32)
  std::wstring wide_name, wide_value;
  if (!Utf8ToWide(name, wide_name) || wide_name.empty()) return false;
  if (!value.present) {
    if (SetEnvironmentVariableW(wide_name.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ENVVAR_NOT_FOUND;
  }
  if (!Utf8ToWide(value.value, wide_value)) return false;
  return SetEnvironmentVariableW(wide_name.c_str(), wide_value.c_str()) != FALSE;
#else
  const std::string key(name);
  if (!value.present) return unsetenv(key.c_str()) == 0;
  return setenv(key.c_str(), value.value.c_str(), 1) == 0;
#endif
}

// --- SnowflakeChild -------------------------------------------------------

SnowflakeChild::~SnowflakeChild() {
  if (Owned()) {
    std::string ignored;
    (void)TerminateAndWait(ignored);
  }
#if defined(_WIN32)
  if (native_ != 0) CloseHandle(reinterpret_cast<HANDLE>(native_));
#endif
  native_ = 0;
}

SnowflakeChild::SnowflakeChild(SnowflakeChild&& other) noexcept
    : native_(std::exchange(other.native_, 0)) {}

SnowflakeChild& SnowflakeChild::operator=(SnowflakeChild&& other) noexcept {
  if (this != &other) {
    if (Owned()) {
      std::string ignored;
      (void)TerminateAndWait(ignored);
#if defined(_WIN32)
      if (native_ != 0) CloseHandle(reinterpret_cast<HANDLE>(native_));
#endif
    }
    native_ = std::exchange(other.native_, 0);
  }
  return *this;
}

bool SnowflakeChild::Wait(std::uint32_t timeout_ms, int& exit_code,
                          std::string& diagnostic) noexcept {
  if (!Owned()) {
    diagnostic = "no owned child to wait for";
    return false;
  }
#if defined(_WIN32)
  const HANDLE process = reinterpret_cast<HANDLE>(native_);
  const DWORD waited = WaitForSingleObject(process, timeout_ms);
  if (waited == WAIT_TIMEOUT) {
    diagnostic = "child did not exit within " + std::to_string(timeout_ms) + " ms";
    return false;
  }
  if (waited != WAIT_OBJECT_0) {
    diagnostic = "WaitForSingleObject failed: " + std::to_string(GetLastError());
    return false;
  }
  DWORD code = 0;
  if (!GetExitCodeProcess(process, &code)) {
    diagnostic = "GetExitCodeProcess failed: " + std::to_string(GetLastError());
    return false;
  }
  CloseHandle(process);
  native_ = 0;
  exit_code = static_cast<int>(code);
  return true;
#else
  const auto pid = static_cast<pid_t>(native_);
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  for (;;) {
    int status = 0;
    const pid_t done = waitpid(pid, &status, WNOHANG);
    if (done == pid) {
      native_ = 0;
      if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
      } else if (WIFSIGNALED(status)) {
        exit_code = 128 + WTERMSIG(status);
        diagnostic = "child terminated by signal " + std::to_string(WTERMSIG(status));
      } else {
        exit_code = 255;
        diagnostic = "child ended with unexpected wait status";
      }
      return true;
    }
    if (done < 0 && errno != EINTR) {
      diagnostic = "waitpid failed: " + std::to_string(errno);
      return false;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      diagnostic = "child did not exit within " + std::to_string(timeout_ms) + " ms";
      return false;
    }
    Sleep1ms();
  }
#endif
}

bool SnowflakeChild::TerminateAndWait(std::string& diagnostic) noexcept {
  if (!Owned()) return true;
#if defined(_WIN32)
  const HANDLE process = reinterpret_cast<HANDLE>(native_);
  if (!TerminateProcess(process, 0xDEADu)) {
    const DWORD error = GetLastError();
    // The child may already have exited; reaping below decides.
    diagnostic = "TerminateProcess failed: " + std::to_string(error);
  }
#else
  if (kill(static_cast<pid_t>(native_), SIGKILL) != 0 && errno != ESRCH)
    diagnostic = "kill failed: " + std::to_string(errno);
#endif
  int ignored = 0;
  std::string wait_diagnostic;
  if (!Wait(30000, ignored, wait_diagnostic)) {
    diagnostic += (diagnostic.empty() ? "" : "; ") + wait_diagnostic;
    return false;
  }
  return true;
}

std::uint64_t SnowflakeChildLaunchAttemptsForTesting() noexcept {
  return launch_attempts.load(std::memory_order_relaxed);
}

bool StartSnowflakeChild(const SnowflakeChildSpec& spec, SnowflakeChild& child,
                         std::string& diagnostic) noexcept {
  if (child.Owned()) {
    diagnostic = "destination child object already owns a process";
    return false;
  }
  if (spec.executable.empty()) {
    diagnostic = "executable path is empty";
    return false;
  }
  if (!spec.executable.is_absolute()) {
    diagnostic = "executable path is not absolute";
    return false;
  }
  if (spec.working_directory.empty() || !spec.working_directory.is_absolute()) {
    diagnostic = "working directory must be a non-empty absolute path";
    return false;
  }
  std::error_code ec;
  if (!std::filesystem::is_directory(spec.working_directory, ec) || ec) {
    diagnostic = "working directory does not exist: " + ErrorText(ec);
    return false;
  }
  for (std::size_t i = 0; i < spec.arguments.size(); ++i) {
    if (!ValidChildText("argument " + std::to_string(i), spec.arguments[i], diagnostic))
      return false;
  }
  if (spec.state_directory.present &&
      !ValidChildText("state directory value", spec.state_directory.value, diagnostic))
    return false;
  if (spec.machine_id.present &&
      !ValidChildText("machine id value", spec.machine_id.value, diagnostic))
    return false;

#if defined(_WIN32)
  const std::wstring& executable = spec.executable.native();
  std::wstring command = L"\"" + executable + L"\"";
  for (const auto& argument : spec.arguments) {
    std::wstring wide;
    if (!Utf8ToWide(argument, wide)) {
      diagnostic = "argument UTF-8 conversion failed";
      return false;
    }
    AppendQuoted(command, wide);
  }
  std::vector<std::wstring> entries;
  if (!EnvironmentEntries(entries, diagnostic)) return false;
  const std::wstring state_name = L"BEDROCK_COMMON_STATE_DIR";
  const std::wstring machine_name = L"BEDROCK_SNOWFLAKE_MACHINE_ID";
  std::erase_if(entries, [&](const std::wstring& entry) {
    const std::wstring_view name = EntryName(entry);
    return SameNameIgnoreCase(name, state_name) ||
           SameNameIgnoreCase(name, machine_name);
  });
  const auto add = [&](const std::wstring& name, const ChildEnvironmentValue& value) {
    if (!value.present) return true;
    std::wstring wide;
    if (!Utf8ToWide(value.value, wide)) return false;
    entries.push_back(name + L"=" + wide);
    return true;
  };
  if (!add(state_name, spec.state_directory) || !add(machine_name, spec.machine_id)) {
    diagnostic = "environment value UTF-8 conversion failed";
    return false;
  }
  std::stable_sort(entries.begin(), entries.end(),
                   [](const std::wstring& a, const std::wstring& b) {
                     const std::wstring_view an = EntryName(a), bn = EntryName(b);
                     return CompareStringOrdinal(an.data(), static_cast<int>(an.size()),
                                                 bn.data(), static_cast<int>(bn.size()),
                                                 TRUE) == CSTR_LESS_THAN;
                   });
  std::wstring block;
  for (const auto& entry : entries) {
    block += entry;
    block.push_back(L'\0');
  }
  block.push_back(L'\0');
  if (entries.empty()) block.push_back(L'\0');

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION info{};
  launch_attempts.fetch_add(1, std::memory_order_relaxed);
  if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                      CREATE_UNICODE_ENVIRONMENT, block.data(),
                      spec.working_directory.c_str(), &startup, &info)) {
    diagnostic = "CreateProcessW failed: " + std::to_string(GetLastError());
    return false;
  }
  CloseHandle(info.hThread);
  child.native_ = reinterpret_cast<std::uintptr_t>(info.hProcess);
  return true;
#else
  const std::string executable = spec.executable.native();
  const std::string directory = spec.working_directory.native();
  std::vector<std::string> argument_storage;
  argument_storage.reserve(spec.arguments.size() + 1);
  argument_storage.push_back(executable);
  for (const auto& argument : spec.arguments) argument_storage.push_back(argument);
  std::vector<char*> argument_pointers;
  for (auto& argument : argument_storage) argument_pointers.push_back(argument.data());
  argument_pointers.push_back(nullptr);

  const auto is_snowflake = [](std::string_view entry) {
    const std::size_t equals = entry.find('=');
    const std::string_view name =
        equals == std::string_view::npos ? entry : entry.substr(0, equals);
    return name == kStateDirectoryVariable || name == kMachineIdVariable;
  };
  std::vector<std::string> environment_storage;
  for (char** item = environ; item != nullptr && *item != nullptr; ++item) {
    if (!is_snowflake(*item)) environment_storage.emplace_back(*item);
  }
  if (spec.state_directory.present)
    environment_storage.push_back(std::string(kStateDirectoryVariable) + "=" +
                                  spec.state_directory.value);
  if (spec.machine_id.present)
    environment_storage.push_back(std::string(kMachineIdVariable) + "=" +
                                  spec.machine_id.value);
  std::vector<char*> environment_pointers;
  for (auto& entry : environment_storage) environment_pointers.push_back(entry.data());
  environment_pointers.push_back(nullptr);

  int report_pipe[2] = {-1, -1};
  if (pipe2(report_pipe, O_CLOEXEC) != 0) {
    diagnostic = "pipe2 failed: " + std::to_string(errno);
    return false;
  }
  launch_attempts.fetch_add(1, std::memory_order_relaxed);
  const pid_t pid = fork();
  if (pid == 0) {
    // Only async-signal-safe calls between fork and exec; Snowflake is never
    // called in the forked image.
    close(report_pipe[0]);
    int failure = 0;
    if (chdir(directory.c_str()) != 0) {
      failure = errno;
    } else {
      execve(executable.c_str(), argument_pointers.data(), environment_pointers.data());
      failure = errno;
    }
    ssize_t ignored = write(report_pipe[1], &failure, sizeof(failure));
    (void)ignored;
    _exit(127);
  }
  close(report_pipe[1]);
  if (pid < 0) {
    diagnostic = "fork failed: " + std::to_string(errno);
    close(report_pipe[0]);
    return false;
  }
  int child_errno = 0;
  ssize_t got = 0;
  do {
    got = read(report_pipe[0], &child_errno, sizeof(child_errno));
  } while (got < 0 && errno == EINTR);
  close(report_pipe[0]);
  if (got != 0) {
    // A complete errno means chdir/execve failed; anything else leaves the
    // launch state unknown, so the child is killed before it is reaped.
    const bool reported = got == static_cast<ssize_t>(sizeof(child_errno));
    if (!reported) (void)kill(pid, SIGKILL);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    diagnostic = reported ? "chdir/execve failed in child: " + std::to_string(child_errno)
                          : "launch status pipe failed";
    return false;
  }
  child.native_ = static_cast<std::uintptr_t>(pid);
  return true;
#endif
}

// --- Reports ----------------------------------------------------------------

const std::string* ChildReport::Find(std::string_view key) const noexcept {
  for (const auto& entry : entries_)
    if (entry.first == key) return &entry.second;
  return nullptr;
}

bool ChildReport::Int64(std::string_view key, std::int64_t& value,
                        std::string& diagnostic) const {
  const std::string* text = Find(key);
  if (text == nullptr) {
    diagnostic = "report has no key " + std::string(key);
    return false;
  }
  const bool canonical = !text->empty() && *text != "-0" &&
                         !(text->size() > 1 && (*text)[0] == '0') &&
                         !(text->size() > 2 && (*text)[0] == '-' && (*text)[1] == '0');
  std::int64_t parsed = 0;
  const auto result = std::from_chars(text->data(), text->data() + text->size(), parsed);
  if (!canonical || result.ec != std::errc{} || result.ptr != text->data() + text->size()) {
    diagnostic = "report key " + std::string(key) + " is not a full-width int64: '" + *text + "'";
    return false;
  }
  value = parsed;
  return true;
}

bool ChildReport::UInt64(std::string_view key, std::uint64_t& value,
                         std::string& diagnostic) const {
  const std::string* text = Find(key);
  if (text == nullptr) {
    diagnostic = "report has no key " + std::string(key);
    return false;
  }
  const bool canonical = !text->empty() && (*text)[0] != '-' && (*text)[0] != '+' &&
                         !(text->size() > 1 && (*text)[0] == '0');
  std::uint64_t parsed = 0;
  const auto result = std::from_chars(text->data(), text->data() + text->size(), parsed);
  if (!canonical || result.ec != std::errc{} || result.ptr != text->data() + text->size()) {
    diagnostic = "report key " + std::string(key) + " is not a full-width uint64: '" + *text + "'";
    return false;
  }
  value = parsed;
  return true;
}

bool ReadChildReport(const std::filesystem::path& path,
                     std::string_view expected_case_id,
                     std::initializer_list<std::string_view> required_keys,
                     ChildReport& report, std::string& diagnostic) {
  report.entries_.clear();
  std::string bytes;
  if (!ReadFileBytes(path, bytes, diagnostic)) return false;
  if (bytes.empty() || bytes.back() != '\n') {
    diagnostic = "report is empty or not newline-terminated";
    return false;
  }
  std::vector<std::pair<std::string, std::string>> entries;
  std::size_t start = 0;
  while (start < bytes.size()) {
    const std::size_t end = bytes.find('\n', start);
    const std::string_view line(bytes.data() + start, end - start);
    start = end + 1;
    const std::size_t equals = line.find('=');
    if (equals == std::string_view::npos || equals == 0) {
      diagnostic = "malformed report line: '" + std::string(line) + "'";
      return false;
    }
    const std::string_view key = line.substr(0, equals);
    for (const char c : key) {
      if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
        diagnostic = "invalid report key: '" + std::string(key) + "'";
        return false;
      }
    }
    const std::string_view value = line.substr(equals + 1);
    if (value.find('\r') != std::string_view::npos || HasNul(value)) {
      diagnostic = "report value contains CR or NUL";
      return false;
    }
    for (const auto& entry : entries) {
      if (entry.first == key) {
        diagnostic = "duplicate report key: " + std::string(key);
        return false;
      }
    }
    entries.emplace_back(std::string(key), std::string(value));
  }
  if (entries.size() < 3 || entries.front().first != "protocol" ||
      entries.front().second != "1") {
    diagnostic = "report does not start with protocol=1";
    return false;
  }
  if (entries[1].first != "case_id" || entries[1].second != expected_case_id) {
    diagnostic = "report case_id mismatch: got '" + entries[1].second + "', expected '" +
                 std::string(expected_case_id) + "'";
    return false;
  }
  if (entries.back().first != "complete" || entries.back().second != "1") {
    diagnostic = "report does not end with complete=1";
    return false;
  }
  report.entries_ = std::move(entries);
  for (const auto key : required_keys) {
    if (report.Find(key) == nullptr) {
      diagnostic = "report is missing required key " + std::string(key);
      report.entries_.clear();
      return false;
    }
  }
  return true;
}

ChildReportWriter::ChildReportWriter(std::string_view case_id) {
  text_ = "protocol=1\ncase_id=";
  text_.append(case_id);
  text_.push_back('\n');
}

void ChildReportWriter::AddInt(std::string_view key, std::int64_t value) {
  AddText(key, std::to_string(value));
}

void ChildReportWriter::AddUInt(std::string_view key, std::uint64_t value) {
  AddText(key, std::to_string(value));
}

void ChildReportWriter::AddText(std::string_view key, std::string_view value) {
  text_.append(key);
  text_.push_back('=');
  text_.append(value);
  text_.push_back('\n');
}

bool ChildReportWriter::Commit(const std::filesystem::path& path,
                               std::string& diagnostic) const {
  std::filesystem::path temporary = path;
  temporary += ".tmp";
  if (!WriteFileBytes(temporary, text_ + "complete=1\n", diagnostic)) return false;
  std::error_code ec;
  std::filesystem::rename(temporary, path, ec);
  if (ec) {
    diagnostic = "cannot publish report: " + ErrorText(ec);
    return false;
  }
  return true;
}

// --- Files and directories ------------------------------------------------

bool WriteFileBytes(const std::filesystem::path& path, std::string_view bytes,
                    std::string& diagnostic) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    diagnostic = "cannot open for writing: " + PathToUtf8(path);
    return false;
  }
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  out.flush();
  const bool written = out.good();
  out.close();
  if (!written || out.fail()) {
    diagnostic = "write/flush/close failed: " + PathToUtf8(path);
    return false;
  }
  return true;
}

bool ReadFileBytes(const std::filesystem::path& path, std::string& bytes,
                   std::string& diagnostic) {
  // Read exactly the current size with native calls: buffered streams read
  // ahead past EOF, which Windows rejects when a live Snowflake issuer holds
  // its slot lock beyond the 192-byte registry.
  bytes.clear();
  constexpr std::uint64_t kLimit = 64ull * 1024 * 1024;
#if defined(_WIN32)
  const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    diagnostic = "cannot open for reading: " + PathToUtf8(path) + " (error " +
                 std::to_string(GetLastError()) + ")";
    return false;
  }
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
      static_cast<std::uint64_t>(size.QuadPart) > kLimit) {
    diagnostic = "cannot size " + PathToUtf8(path) + " (error " + std::to_string(GetLastError()) + ")";
    CloseHandle(file);
    return false;
  }
  std::string data(static_cast<std::size_t>(size.QuadPart), '\0');
  std::size_t done = 0;
  while (done < data.size()) {
    DWORD got = 0;
    const auto chunk = static_cast<DWORD>(std::min<std::size_t>(data.size() - done, 1u << 20));
    if (!ReadFile(file, data.data() + done, chunk, &got, nullptr)) {
      diagnostic = "ReadFile failed for " + PathToUtf8(path) + " (error " +
                   std::to_string(GetLastError()) + ")";
      CloseHandle(file);
      return false;
    }
    if (got == 0) break;
    done += got;
  }
  CloseHandle(file);
#else
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    diagnostic = "cannot open for reading: " + PathToUtf8(path) + " (errno " +
                 std::to_string(errno) + ")";
    return false;
  }
  struct stat info {};
  if (::fstat(fd, &info) != 0 || info.st_size < 0 ||
      static_cast<std::uint64_t>(info.st_size) > kLimit) {
    diagnostic = "cannot size " + PathToUtf8(path) + " (errno " + std::to_string(errno) + ")";
    ::close(fd);
    return false;
  }
  std::string data(static_cast<std::size_t>(info.st_size), '\0');
  std::size_t done = 0;
  while (done < data.size()) {
    const ssize_t got = ::read(fd, data.data() + done, data.size() - done);
    if (got < 0 && errno == EINTR) continue;
    if (got < 0) {
      diagnostic = "read failed for " + PathToUtf8(path) + " (errno " + std::to_string(errno) + ")";
      ::close(fd);
      return false;
    }
    if (got == 0) break;
    done += static_cast<std::size_t>(got);
  }
  ::close(fd);
#endif
  if (done != data.size()) {
    diagnostic = "short read: " + PathToUtf8(path);
    return false;
  }
  bytes = std::move(data);
  return true;
}

bool FileExists(const std::filesystem::path& path, bool& exists,
                std::string& diagnostic) {
  std::error_code ec;
  exists = std::filesystem::exists(path, ec);
  if (ec) {
    diagnostic = "cannot query " + PathToUtf8(path) + ": " + ErrorText(ec);
    return false;
  }
  return true;
}

ScopedTestDirectory::~ScopedTestDirectory() {
  std::string ignored;
  (void)Remove(ignored);
}

bool ScopedTestDirectory::Create(std::string_view label, std::string& diagnostic) {
  if (!path_.empty()) {
    diagnostic = "directory already created";
    return false;
  }
  if (!IsValidUtf8(label) || HasNul(label)) {
    diagnostic = "directory label is not valid UTF-8";
    return false;
  }
  static std::atomic<std::uint64_t> counter{0};
  std::error_code ec;
  const auto root = std::filesystem::temp_directory_path(ec);
  if (ec) {
    diagnostic = "temp_directory_path failed: " + ErrorText(ec);
    return false;
  }
#if defined(_WIN32)
  const auto process = static_cast<std::uint64_t>(GetCurrentProcessId());
#else
  const auto process = static_cast<std::uint64_t>(getpid());
#endif
  for (int attempt = 0; attempt < 100; ++attempt) {
    const auto ticks = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string name = "bedrock-sf-" + std::string(label) + "-" +
                             std::to_string(process) + "-" +
                             std::to_string(counter.fetch_add(1)) + "-" +
                             std::to_string(ticks % 1000000007u);
    const auto candidate = std::filesystem::absolute(root / PathFromUtf8(name), ec);
    if (ec) {
      diagnostic = "absolute() failed: " + ErrorText(ec);
      return false;
    }
    if (std::filesystem::create_directory(candidate, ec)) {
      path_ = candidate;
      return true;
    }
    if (ec) {
      diagnostic = "create_directory failed: " + ErrorText(ec);
      return false;
    }
  }
  diagnostic = "could not create a unique test directory";
  return false;
}

bool ScopedTestDirectory::Remove(std::string& diagnostic) {
  if (path_.empty()) return true;
  std::error_code ec;
  std::filesystem::remove_all(path_, ec);
  if (ec) {
    diagnostic = "remove_all failed: " + ErrorText(ec);
    return false;
  }
  path_.clear();
  return true;
}

// --- Helper invocation ------------------------------------------------------

bool MakeHelperSpec(std::string_view mode, const std::filesystem::path& report_path,
                    std::string_view case_id,
                    const std::vector<std::string>& extra_arguments,
                    const std::filesystem::path& working_directory,
                    ChildEnvironmentValue state_directory,
                    ChildEnvironmentValue machine_id, SnowflakeChildSpec& spec,
                    std::string& diagnostic) {
  spec = SnowflakeChildSpec{};
  if (!CurrentExecutablePath(spec.executable, diagnostic)) return false;
  spec.working_directory = working_directory;
  spec.arguments = {std::string(kSnowflakeHelperFlag), std::string(mode),
                    PathToUtf8(report_path), std::string(case_id)};
  spec.arguments.insert(spec.arguments.end(), extra_arguments.begin(),
                        extra_arguments.end());
  spec.state_directory = std::move(state_directory);
  spec.machine_id = std::move(machine_id);
  return true;
}

bool WaitForFileWhileChildRuns(SnowflakeChild& child,
                               const std::filesystem::path& path,
                               std::uint32_t timeout_ms, std::string& diagnostic) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    bool exists = false;
    if (!FileExists(path, exists, diagnostic)) return false;
    if (exists) return true;
    int exit_code = 0;
    std::string wait_diagnostic;
    if (child.Wait(0, exit_code, wait_diagnostic)) {
      diagnostic = "child exited with " + std::to_string(exit_code) +
                   " before publishing " + PathToUtf8(path) + " " + wait_diagnostic;
      return false;
    }
    if (!child.Owned()) {
      diagnostic = "child is not owned: " + wait_diagnostic;
      return false;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      diagnostic = "timed out waiting for " + PathToUtf8(path);
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

bool RunSnowflakeHelper(const SnowflakeChildSpec& spec,
                        const std::filesystem::path& report_path,
                        std::string_view case_id,
                        std::initializer_list<std::string_view> required_keys,
                        ChildReport& report, std::string& diagnostic,
                        std::uint32_t timeout_ms) {
  SnowflakeChild child;
  if (!StartSnowflakeChild(spec, child, diagnostic)) return false;
  int exit_code = -1;
  std::string wait_diagnostic;
  if (!child.Wait(timeout_ms, exit_code, wait_diagnostic)) {
    std::string kill_diagnostic;
    (void)child.TerminateAndWait(kill_diagnostic);
    diagnostic = "child wait failed: " + wait_diagnostic + " " + kill_diagnostic;
    return false;
  }
  if (exit_code != 0) {
    std::filesystem::path error_path = report_path;
    error_path += ".error";
    std::string child_error, ignored;
    (void)ReadFileBytes(error_path, child_error, ignored);
    diagnostic = "child harness failure, exit code " + std::to_string(exit_code) +
                 " " + wait_diagnostic + " " + child_error;
    return false;
  }
  return ReadChildReport(report_path, case_id, required_keys, report, diagnostic);
}

}  // namespace bedrock::test
