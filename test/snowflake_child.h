/**
 * @file snowflake_child.h
 * @brief Fresh-process Snowflake child launcher and report protocol shared by
 *        common_external_tests and common_internal_tests.
 *
 * A child is the running test executable itself, started with
 *
 *   <test-exe> --sf-helper <mode> <report-path> <case-id> [mode arguments...]
 *
 * test_main.cc dispatches helper modes before GoogleTest initialization and
 * before ScopedSnowflakeConfig, so every child observes only the environment
 * chosen by its SnowflakeChildSpec. Launching common_external_tests exercises
 * the real Common library boundary; launching common_internal_tests exercises
 * the internal static clone.
 *
 * On Windows the child obtains its arguments from GetCommandLineW() and
 * CommandLineToArgvW() and converts them to UTF-8 explicitly (the ANSI argv of
 * main() is never assumed to be UTF-8). Paths are opened through native
 * std::filesystem::path objects created from UTF-8.
 *
 * Report protocol (version 1)
 * ---------------------------
 * A report is a sequence of `key=value\n` lines. Keys are [a-z0-9_]+. Integer
 * values are full-width signed or unsigned decimal. The first line is
 * `protocol=1`, the second `case_id=<case-id argument, UTF-8 bytes>`, the last
 * `complete=1`. Each key occurs once. The child writes `<report>.tmp`, flushes
 * and closes it, then renames it to `<report>`, so a visible report is always
 * complete. ReadChildReport() rejects a missing trailing newline, missing
 * required keys, duplicate keys, malformed lines and a different case id.
 *
 * A child that finished reporting exits with 0; library errors are values in
 * the report, never exit codes (POSIX keeps only 8 bits of an exit status).
 * A nonzero exit is a harness failure; the child then tries to leave a
 * one-line diagnostic in `<report>.error`.
 *
 * Modes and report keys (all values are the actual call results):
 *
 *   probe-config <report> <case-id>
 *     result            Snowflake::Generate() (signed; >= 0 is an ID).
 *
 *   registry-seed <report> <case-id>
 *     result            Same as probe-config; used to create a registry.
 *
 *   probe-cache <first-report> <case-id> <continue-file> <final-report>
 *               <next-state-dir> <next-machine-id>
 *     first report:     first_result
 *     final report:     first_result, second_result, thread_result
 *     The child generates once, publishes the first report, waits (bounded)
 *     for <continue-file>, sets BEDROCK_COMMON_STATE_DIR=<next-state-dir> and
 *     BEDROCK_SNOWFLAKE_MACHINE_ID=<next-machine-id> in its own environment,
 *     then generates once on the same thread (second_result) and once on a new
 *     joined thread (thread_result).
 *
 *   probe-archive <report> <case-id> <submode>
 *     Schema: one `value` int32 field; Accept() counts its calls.
 *     submode `dump`:   YAMLSerializer::Dump(schema{value=1})
 *       dump_code, dump_get_status, dump_failed (Status::Failed() of the
 *       returned status, 0/1), output_size (Output().size()).
 *     submode `initialize`: YAMLDeserializer::Initialize("value: 1\n")
 *       initialize_code, initialize_get_status, initialize_failed (0/1).
 *     submode `load-after-failure`: Initialize("value: 1\n") followed by
 *       Load(destination{value=99}) on the same deserializer, whatever the
 *       Initialize outcome was (also usable as the valid-configuration control)
 *       initialize_code, initialize_get_status, load_code, load_get_status,
 *       destination_value, accept_calls.
 *     All *_code and *_get_status values are the unsigned decimal of
 *     bedrock::ErrorCode (0x101 is reported as 257).
 *
 *   generate <report> <case-id> <count> <ids-path>
 *     requested, generated, last_result. Writes one decimal ID per line to
 *     <ids-path>; stops at the first negative result (no retry).
 *
 *   hold <report> <case-id> <release-file>
 *     result. The report is the readiness barrier; a successful issuer then
 *     keeps its slot until <release-file> exists (bounded wait, exit 95 when
 *     the parent never releases it).
 *
 *   probe-fixture <report> <case-id>
 *     ScopedSnowflakeConfig("3") lifecycle inside a fresh process:
 *     state_present_before, machine_present_before, fixture_ok,
 *     state_matches_during, machine_during_is_3, machine_file_is_3,
 *     state_present_after, state_value_after (text),
 *     machine_present_after, machine_value_after (text), directory_removed.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bedrock::test {

inline constexpr std::string_view kSnowflakeHelperFlag = "--sf-helper";
inline constexpr std::string_view kStateDirectoryVariable = "BEDROCK_COMMON_STATE_DIR";
inline constexpr std::string_view kMachineIdVariable = "BEDROCK_SNOWFLAKE_MACHINE_ID";
/** Harness limit for one child; not a generator policy. */
inline constexpr std::uint32_t kDefaultChildTimeoutMs = 60000;

/** Value of one Snowflake environment variable in a child environment. */
struct ChildEnvironmentValue {
  bool present = false;
  std::string value;  // UTF-8; meaningful only when present.

  [[nodiscard]] static ChildEnvironmentValue Absent() { return {}; }
  [[nodiscard]] static ChildEnvironmentValue Present(std::string text) {
    return {true, std::move(text)};
  }
};

struct SnowflakeChildSpec {
  std::filesystem::path executable;         // explicit; never inferred from argv[0]
  std::filesystem::path working_directory;  // existing, task-owned directory
  std::vector<std::string> arguments;       // UTF-8, excluding argv[0]
  ChildEnvironmentValue state_directory;    // BEDROCK_COMMON_STATE_DIR
  ChildEnvironmentValue machine_id;         // BEDROCK_SNOWFLAKE_MACHINE_ID
};

/**
 * Move-only owner of one started child. It owns and reaps only the child it
 * started. The destructor terminates and reaps a child that is still owned,
 * so an assertion failure cannot leave a test-created process behind.
 */
class SnowflakeChild final {
 public:
  SnowflakeChild() noexcept = default;
  ~SnowflakeChild();
  SnowflakeChild(SnowflakeChild&& other) noexcept;
  SnowflakeChild& operator=(SnowflakeChild&& other) noexcept;
  SnowflakeChild(const SnowflakeChild&) = delete;
  SnowflakeChild& operator=(const SnowflakeChild&) = delete;

  /** True while a started child has not been reaped. */
  [[nodiscard]] bool Owned() const noexcept { return native_ != 0; }
  /**
   * Waits up to timeout_ms. Returns true after the child exited and was
   * reaped; exit_code then holds its exit status (128 + signal on POSIX
   * signal termination, with a diagnostic). Returns false on timeout (the
   * child stays owned) or on a wait failure.
   */
  [[nodiscard]] bool Wait(std::uint32_t timeout_ms, int& exit_code,
                          std::string& diagnostic) noexcept;
  /** Kills the owned child and reaps it within a bounded time. */
  bool TerminateAndWait(std::string& diagnostic) noexcept;

 private:
  friend bool StartSnowflakeChild(const SnowflakeChildSpec&, SnowflakeChild&,
                                  std::string&) noexcept;
  std::uintptr_t native_ = 0;  // HANDLE on Windows, pid on POSIX.
};

/**
 * Starts a child. Validation (empty/relative executable, missing working
 * directory, invalid UTF-8 or NUL in arguments/environment values) fails
 * before any process-creation call. Returns false with a diagnostic and
 * leaves `child` unowned on any failure; never asserts by itself.
 */
bool StartSnowflakeChild(const SnowflakeChildSpec& spec, SnowflakeChild& child,
                         std::string& diagnostic) noexcept;

/** Number of times StartSnowflakeChild reached CreateProcessW/fork. */
[[nodiscard]] std::uint64_t SnowflakeChildLaunchAttemptsForTesting() noexcept;

/** Strict UTF-8 check: rejects overlong forms, surrogates, > U+10FFFF. */
[[nodiscard]] bool IsValidUtf8(std::string_view text) noexcept;
#if defined(_WIN32)
/** Strict conversion; empty input succeeds with empty output. */
[[nodiscard]] bool Utf8ToWide(std::string_view utf8, std::wstring& wide);
[[nodiscard]] bool WideToUtf8(std::wstring_view wide, std::string& utf8);
#endif
/** UTF-8 bytes of a native path (no ANSI code page conversion). */
[[nodiscard]] std::string PathToUtf8(const std::filesystem::path& path);
/** Native path from UTF-8 bytes; the caller validates UTF-8 first. */
[[nodiscard]] std::filesystem::path PathFromUtf8(std::string_view utf8);

/** Absolute path of the running test executable. */
bool CurrentExecutablePath(std::filesystem::path& path,
                           std::string& diagnostic) noexcept;

/** Child helper arguments as UTF-8 (native wide command line on Windows). */
bool NativeHelperArguments(int argc, char** argv, std::vector<std::string>& args,
                           std::string& diagnostic);

/** Environment of the current process; distinguishes absent and empty. */
bool ReadProcessEnvironment(std::string_view name, bool& present,
                            std::string& value);
/** Sets (present) or removes (absent) a variable of the current process. */
bool WriteProcessEnvironment(std::string_view name,
                             const ChildEnvironmentValue& value);

/** Parsed protocol-1 report. */
class ChildReport final {
 public:
  [[nodiscard]] const std::string* Find(std::string_view key) const noexcept;
  [[nodiscard]] bool Int64(std::string_view key, std::int64_t& value,
                           std::string& diagnostic) const;
  [[nodiscard]] bool UInt64(std::string_view key, std::uint64_t& value,
                            std::string& diagnostic) const;
  [[nodiscard]] const std::vector<std::pair<std::string, std::string>>&
  Entries() const noexcept {
    return entries_;
  }

 private:
  friend bool ReadChildReport(const std::filesystem::path&, std::string_view,
                              std::initializer_list<std::string_view>,
                              ChildReport&, std::string&);
  std::vector<std::pair<std::string, std::string>> entries_;
};

/** Reads and validates a complete report (see protocol above). */
bool ReadChildReport(const std::filesystem::path& path,
                     std::string_view expected_case_id,
                     std::initializer_list<std::string_view> required_keys,
                     ChildReport& report, std::string& diagnostic);

/** Child-side report builder; Commit() publishes atomically via rename. */
class ChildReportWriter final {
 public:
  explicit ChildReportWriter(std::string_view case_id);
  void AddInt(std::string_view key, std::int64_t value);
  void AddUInt(std::string_view key, std::uint64_t value);
  void AddText(std::string_view key, std::string_view value);
  [[nodiscard]] bool Commit(const std::filesystem::path& path,
                            std::string& diagnostic) const;

 private:
  std::string text_;
};

/** Unique existing directory under the temp root, removed recursively. */
class ScopedTestDirectory final {
 public:
  ScopedTestDirectory() = default;
  ~ScopedTestDirectory();
  ScopedTestDirectory(const ScopedTestDirectory&) = delete;
  ScopedTestDirectory& operator=(const ScopedTestDirectory&) = delete;

  /** label is UTF-8 and becomes part of the directory name. */
  [[nodiscard]] bool Create(std::string_view label, std::string& diagnostic);
  [[nodiscard]] const std::filesystem::path& Path() const noexcept {
    return path_;
  }
  bool Remove(std::string& diagnostic);

 private:
  std::filesystem::path path_;
};

bool WriteFileBytes(const std::filesystem::path& path, std::string_view bytes,
                    std::string& diagnostic);
bool ReadFileBytes(const std::filesystem::path& path, std::string& bytes,
                   std::string& diagnostic);
bool FileExists(const std::filesystem::path& path, bool& exists,
                std::string& diagnostic);

/** Spec for `<self> --sf-helper <mode> <report> <case-id> extra...`. */
bool MakeHelperSpec(std::string_view mode,
                    const std::filesystem::path& report_path,
                    std::string_view case_id,
                    const std::vector<std::string>& extra_arguments,
                    const std::filesystem::path& working_directory,
                    ChildEnvironmentValue state_directory,
                    ChildEnvironmentValue machine_id, SnowflakeChildSpec& spec,
                    std::string& diagnostic);

/**
 * Polls until `path` exists. Fails early when the child exits first (the
 * child is then reaped) and on timeout (the child stays owned).
 */
bool WaitForFileWhileChildRuns(SnowflakeChild& child,
                               const std::filesystem::path& path,
                               std::uint32_t timeout_ms,
                               std::string& diagnostic);

/**
 * Starts the child, waits for it (terminating and reaping it on timeout),
 * requires exit code 0 and reads the validated report.
 */
bool RunSnowflakeHelper(const SnowflakeChildSpec& spec,
                        const std::filesystem::path& report_path,
                        std::string_view case_id,
                        std::initializer_list<std::string_view> required_keys,
                        ChildReport& report, std::string& diagnostic,
                        std::uint32_t timeout_ms = kDefaultChildTimeoutMs);

}  // namespace bedrock::test
