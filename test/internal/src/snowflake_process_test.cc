// Fresh-process Snowflake configuration, cache, registry, launcher and
// ownership tests. Every library observation runs in a newly executed child
// of this test binary (see snowflake_child.h); the parent only prepares files
// and environment blocks and inspects reports and bytes on disk.
#include "common/data_types/snowflake.h"
#include "snowflake_child.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;
using bedrock::SnowflakeError;
using bedrock::test::ChildEnvironmentValue;
using bedrock::test::ChildReport;
using bedrock::test::SnowflakeChild;
using bedrock::test::SnowflakeChildSpec;
using ::testing::AssertionFailure;
using ::testing::AssertionResult;
using ::testing::AssertionSuccess;

constexpr std::int64_t Err(SnowflakeError error) {
  return static_cast<std::int64_t>(error);
}
constexpr int MachineOf(std::int64_t id) { return static_cast<int>((id >> 19) & 7); }
constexpr int SlotOf(std::int64_t id) { return static_cast<int>((id >> 12) & 127); }
constexpr std::size_t kRegistrySize = 192;
constexpr std::uint64_t kEpochMs = 1704067200000ULL;

ChildEnvironmentValue Absent() { return ChildEnvironmentValue::Absent(); }
ChildEnvironmentValue Present(std::string value) {
  return ChildEnvironmentValue::Present(std::move(value));
}

void Note(std::string_view name, std::string_view what, std::int64_t value) {
  std::printf("[sf-case] %.*s %.*s=%lld\n", static_cast<int>(name.size()), name.data(),
              static_cast<int>(what.size()), what.data(), static_cast<long long>(value));
  std::fflush(stdout);
}

// One isolated case: work/ is the child cwd, state/ and state-b/ are
// candidate state directories, reports/ receives child reports. Declare it
// before any SnowflakeChild so children are reaped before removal.
class CaseLayout final {
 public:
  AssertionResult Create(std::string_view label) {
    std::string diagnostic;
    if (!root_.Create(label, diagnostic)) return AssertionFailure() << diagnostic;
    work_ = root_.Path() / "work";
    state_ = root_.Path() / "state";
    state_b_ = root_.Path() / "state-b";
    reports_ = root_.Path() / "reports";
    for (const fs::path* directory : {&work_, &state_, &state_b_, &reports_}) {
      std::error_code ec;
      if (!fs::create_directory(*directory, ec) || ec)
        return AssertionFailure() << "cannot create " << bedrock::test::PathToUtf8(*directory)
                                  << ": " << ec.message();
    }
    return AssertionSuccess();
  }
  const fs::path& Root() const noexcept { return root_.Path(); }
  const fs::path& Work() const noexcept { return work_; }
  const fs::path& State() const noexcept { return state_; }
  const fs::path& StateB() const noexcept { return state_b_; }
  fs::path Report(std::string_view name) const {
    return reports_ / (std::string(name) + ".report");
  }
  std::string StateText() const { return bedrock::test::PathToUtf8(state_); }

 private:
  bedrock::test::ScopedTestDirectory root_;
  fs::path work_, state_, state_b_, reports_;
};

AssertionResult WriteExact(const fs::path& path, std::string_view bytes) {
  std::string diagnostic, back;
  if (!bedrock::test::WriteFileBytes(path, bytes, diagnostic) ||
      !bedrock::test::ReadFileBytes(path, back, diagnostic))
    return AssertionFailure() << diagnostic;
  if (back != bytes) return AssertionFailure() << "bytes not preserved: " << path;
  return AssertionSuccess();
}

AssertionResult ReadExact(const fs::path& path, std::string& bytes) {
  std::string diagnostic;
  if (!bedrock::test::ReadFileBytes(path, bytes, diagnostic)) return AssertionFailure() << diagnostic;
  return AssertionSuccess();
}

AssertionResult WriteMachineFile(const fs::path& directory, std::string_view bytes) {
  return WriteExact(directory / "machine-id", bytes);
}

AssertionResult RegistryAbsent(const fs::path& directory) {
  bool exists = true;
  std::string diagnostic;
  if (!bedrock::test::FileExists(directory / "snowflake.registry", exists, diagnostic))
    return AssertionFailure() << diagnostic;
  if (exists)
    return AssertionFailure() << "unexpected registry in " << bedrock::test::PathToUtf8(directory);
  return AssertionSuccess();
}

AssertionResult MakeSpec(const CaseLayout& layout, std::string_view mode,
                         const fs::path& report, std::string_view case_id,
                         const std::vector<std::string>& extra,
                         ChildEnvironmentValue state, ChildEnvironmentValue machine,
                         SnowflakeChildSpec& spec) {
  std::string diagnostic;
  if (!bedrock::test::MakeHelperSpec(mode, report, case_id, extra, layout.Work(),
                                     std::move(state), std::move(machine), spec, diagnostic))
    return AssertionFailure() << diagnostic;
  return AssertionSuccess();
}

// Runs a one-shot `result` mode (probe-config or registry-seed) in a fresh child.
AssertionResult Probe(const CaseLayout& layout, std::string_view case_id,
                      ChildEnvironmentValue state, ChildEnvironmentValue machine,
                      std::int64_t& result, std::string_view mode = "probe-config") {
  SnowflakeChildSpec spec;
  const fs::path report = layout.Report(case_id);
  if (auto made = MakeSpec(layout, mode, report, case_id, {}, std::move(state),
                           std::move(machine), spec);
      !made)
    return made;
  ChildReport parsed;
  std::string diagnostic;
  if (!bedrock::test::RunSnowflakeHelper(spec, report, case_id, {"result"}, parsed, diagnostic) ||
      !parsed.Int64("result", result, diagnostic))
    return AssertionFailure() << diagnostic;
  Note(case_id, "result", result);
  return AssertionSuccess();
}

std::uint64_t LittleEndian(const std::string& bytes, std::size_t offset, std::size_t width) {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < width; ++i)
    value |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
  return value;
}

void PutLittleEndian(std::string& bytes, std::size_t offset, std::size_t width,
                     std::uint64_t value) {
  for (std::size_t i = 0; i < width; ++i)
    bytes[offset + i] = static_cast<char>((value >> (8 * i)) & 0xFFu);
}

// Exact v1 registry layout for a record created with `machine`.
void ExpectFreshRegistry(const std::string& bytes, int machine) {
  ASSERT_EQ(bytes.size(), kRegistrySize);
  EXPECT_EQ(bytes.substr(0, 8), "BRSNFLK1");
  EXPECT_EQ(LittleEndian(bytes, 8, 2), 1u);
  EXPECT_EQ(LittleEndian(bytes, 10, 2), 64u);
  EXPECT_EQ(LittleEndian(bytes, 12, 4), kRegistrySize);
  EXPECT_EQ(LittleEndian(bytes, 16, 8), kEpochMs);
  EXPECT_EQ(static_cast<unsigned char>(bytes[24]), 41u);
  EXPECT_EQ(static_cast<unsigned char>(bytes[25]), 3u);
  EXPECT_EQ(static_cast<unsigned char>(bytes[26]), 7u);
  EXPECT_EQ(static_cast<unsigned char>(bytes[27]), 12u);
  EXPECT_EQ(static_cast<unsigned char>(bytes[28]), static_cast<unsigned>(machine));
  for (std::size_t i = 29; i < kRegistrySize; ++i)
    EXPECT_EQ(bytes[i], '\0') << "reserved offset " << i;
}

// Dynamic registration keeps exact `Suite.Group/Case` names while every test
// of a suite shares the suite fixture class.
template <typename Fixture>
class DynamicCase final : public Fixture {
 public:
  explicit DynamicCase(std::function<void()> body) : body_(std::move(body)) {}
  void TestBody() override { body_(); }

 private:
  std::function<void()> body_;
};

template <typename Fixture>
void Register(const char* suite, const std::string& name, std::function<void()> body) {
  ::testing::RegisterTest(suite, name.c_str(), nullptr, nullptr, __FILE__, __LINE__,
                          [body]() -> Fixture* { return new DynamicCase<Fixture>(body); });
}

// ---------------------------------------------------------------------------
// SnowflakeConfigProcess: machine-id/state configuration in fresh processes.
// ---------------------------------------------------------------------------
class SnowflakeConfigProcess : public ::testing::Test {};

struct ValueCase {
  const char* name;
  std::string_view bytes;
};

constexpr ValueCase kInvalidValues[] = {
    {"Empty", ""},
    {"WhitespaceOnly", " \t\r\n\v\f"},
    {"Alpha", "x"},
    {"Negative", "-1"},
    {"Signed", "+1"},
    {"LeadingZero", "01"},
    {"TrailingJunk", "1x"},
    {"Bom", "\xEF\xBB\xBF" "1"},
    {"HighDigit", "8"},
    {"LargeNumber", "256"},
};
constexpr ValueCase kFileOnlyInvalidValues[] = {
    {"EmbeddedNul", std::string_view("1\0", 2)},
    {"HighBit", "\xFF"},
};
struct ValidCase {
  const char* name;
  std::string_view bytes;
  int machine;
};
constexpr ValidCase kValidValues[] = {
    {"Zero", "0", 0},
    {"Seven", "7", 7},
    {"Whitespace", " \t7\r\n\v\f", 7},
};

void EnvironmentRejects(const ValueCase& value) {
  SCOPED_TRACE(value.name);
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("env-invalid"));
  ASSERT_TRUE(WriteMachineFile(layout.State(), "4"));
  std::int64_t result = 0;
  ASSERT_TRUE(Probe(layout, std::string("env-invalid-") + value.name, Present(layout.StateText()),
                    Present(std::string(value.bytes)), result));
  EXPECT_EQ(result, Err(SnowflakeError::kInvalidMachineId)) << "no fallback to the valid file";
  EXPECT_TRUE(RegistryAbsent(layout.State()));
  std::string after;
  ASSERT_TRUE(ReadExact(layout.State() / "machine-id", after));
  EXPECT_EQ(after, "4");
}

void FileRejects(const ValueCase& value) {
  SCOPED_TRACE(value.name);
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("file-invalid"));
  ASSERT_TRUE(WriteMachineFile(layout.State(), value.bytes));
  std::int64_t result = 0;
  ASSERT_TRUE(Probe(layout, std::string("file-invalid-") + value.name, Present(layout.StateText()),
                    Absent(), result));
  EXPECT_EQ(result, Err(SnowflakeError::kInvalidMachineId));
  EXPECT_TRUE(RegistryAbsent(layout.State()));
  std::string after;
  ASSERT_TRUE(ReadExact(layout.State() / "machine-id", after));
  EXPECT_EQ(after, std::string(value.bytes)) << "binary bytes are preserved";
}

void AcceptsValue(const ValidCase& value, bool from_environment) {
  SCOPED_TRACE(value.name);
  CaseLayout layout;
  ASSERT_TRUE(layout.Create(from_environment ? "env-valid" : "file-valid"));
  if (!from_environment) {
    ASSERT_TRUE(WriteMachineFile(layout.State(), value.bytes));
  }
  ASSERT_TRUE(RegistryAbsent(layout.State()));
  std::int64_t result = -99;
  ASSERT_TRUE(Probe(layout,
                    std::string(from_environment ? "env-valid-" : "file-valid-") + value.name,
                    Present(layout.StateText()),
                    from_environment ? Present(std::string(value.bytes)) : Absent(), result));
  ASSERT_GE(result, 0);
  EXPECT_EQ(MachineOf(result), value.machine);
  std::string registry;
  ASSERT_TRUE(ReadExact(layout.State() / "snowflake.registry", registry));
  ASSERT_NO_FATAL_FAILURE(ExpectFreshRegistry(registry, value.machine));
}

const bool kConfigMatrixRegistered = [] {
  for (const auto& value : kInvalidValues)
    Register<SnowflakeConfigProcess>("SnowflakeConfigProcess",
                                     std::string("EnvironmentRejectsInvalidValue/") + value.name,
                                     [value] { EnvironmentRejects(value); });
  for (const auto& value : kInvalidValues)
    Register<SnowflakeConfigProcess>("SnowflakeConfigProcess",
                                     std::string("FileRejectsInvalidValue/") + value.name,
                                     [value] { FileRejects(value); });
  for (const auto& value : kFileOnlyInvalidValues)
    Register<SnowflakeConfigProcess>("SnowflakeConfigProcess",
                                     std::string("FileRejectsInvalidValue/") + value.name,
                                     [value] { FileRejects(value); });
  for (const auto& value : kValidValues)
    Register<SnowflakeConfigProcess>(
        "SnowflakeConfigProcess",
        std::string("EnvironmentAcceptsBoundaryAndAsciiWhitespace/") + value.name,
        [value] { AcceptsValue(value, true); });
  for (const auto& value : kValidValues)
    Register<SnowflakeConfigProcess>(
        "SnowflakeConfigProcess",
        std::string("FileAcceptsBoundaryAndAsciiWhitespace/") + value.name,
        [value] { AcceptsValue(value, false); });
  return true;
}();

TEST_F(SnowflakeConfigProcess, MissingEnvAndMissingFileReturnsMissingMachineId) {
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("missing"));
  std::int64_t result = 0;
  ASSERT_TRUE(Probe(layout, "missing", Present(layout.StateText()), Absent(), result));
  EXPECT_EQ(result, Err(SnowflakeError::kMissingMachineId));
  EXPECT_TRUE(RegistryAbsent(layout.State()));
}

TEST_F(SnowflakeConfigProcess, EnvironmentWinsOverDifferentValidFile) {
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("env-wins"));
  ASSERT_TRUE(WriteMachineFile(layout.State(), "5"));
  ASSERT_TRUE(RegistryAbsent(layout.State()));
  std::int64_t result = -99;
  ASSERT_TRUE(Probe(layout, "env-wins", Present(layout.StateText()), Present("6"), result));
  ASSERT_GE(result, 0);
  EXPECT_EQ(MachineOf(result), 6);
  std::string registry;
  ASSERT_TRUE(ReadExact(layout.State() / "snowflake.registry", registry));
  ASSERT_EQ(registry.size(), kRegistrySize);
  EXPECT_EQ(static_cast<unsigned char>(registry[28]), 6u);
}

TEST_F(SnowflakeConfigProcess, EnvironmentWinsOverMalformedFile) {
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("env-wins-malformed"));
  ASSERT_TRUE(WriteMachineFile(layout.State(), "x"));
  std::int64_t result = -99;
  ASSERT_TRUE(Probe(layout, "env-wins-malformed", Present(layout.StateText()), Present("6"), result));
  ASSERT_GE(result, 0);
  EXPECT_EQ(MachineOf(result), 6);
  std::string after;
  ASSERT_TRUE(ReadExact(layout.State() / "machine-id", after));
  EXPECT_EQ(after, "x");
}

TEST_F(SnowflakeConfigProcess, MissingEnvironmentFallsBackToFile) {
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("file-fallback"));
  ASSERT_TRUE(WriteMachineFile(layout.State(), "5"));
  std::int64_t result = -99;
  ASSERT_TRUE(Probe(layout, "file-fallback", Present(layout.StateText()), Absent(), result));
  ASSERT_GE(result, 0);
  EXPECT_EQ(MachineOf(result), 5);
}

TEST_F(SnowflakeConfigProcess, PresentInvalidEnvironmentNeverFallsBack) {
  for (const std::string value : {std::string(), std::string("8")}) {
    SCOPED_TRACE("machine environment '" + value + "'");
    CaseLayout layout;
    ASSERT_TRUE(layout.Create("no-fallback"));
    ASSERT_TRUE(WriteMachineFile(layout.State(), "5"));
    std::int64_t result = 0;
    ASSERT_TRUE(Probe(layout, value.empty() ? "no-fallback-empty" : "no-fallback-8",
                      Present(layout.StateText()), Present(value), result));
    EXPECT_EQ(result, Err(SnowflakeError::kInvalidMachineId));
    EXPECT_TRUE(RegistryAbsent(layout.State()));
  }
}

TEST_F(SnowflakeConfigProcess, OversizedMachineEnvironmentReportsReadFailure) {
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("env-oversized"));
  ASSERT_TRUE(WriteMachineFile(layout.State(), "5"));
  // A prefix of this value would trim to the valid "7"; a partial read must not
  // be used, and a read failure must not fall back to the valid file.
  const std::string value = "7" + std::string(2048, ' ');
  ASSERT_EQ(value.size(), 2049u);
  std::int64_t result = 0;
  ASSERT_TRUE(Probe(layout, "env-oversized", Present(layout.StateText()), Present(value), result));
  EXPECT_EQ(result, Err(SnowflakeError::kEnvironmentReadFailed));
  EXPECT_TRUE(RegistryAbsent(layout.State()));
}

TEST_F(SnowflakeConfigProcess, OversizedMachineFileReportsReadFailure) {
  for (const std::size_t size : {std::size_t{2048}, std::size_t{4096}}) {
    SCOPED_TRACE("file size " + std::to_string(size));
    CaseLayout layout;
    ASSERT_TRUE(layout.Create("file-oversized"));
    const std::string bytes = "7" + std::string(size - 1, ' ');
    ASSERT_TRUE(WriteMachineFile(layout.State(), bytes));
    std::int64_t result = 0;
    ASSERT_TRUE(Probe(layout, "file-oversized-" + std::to_string(size), Present(layout.StateText()),
                      Absent(), result));
    EXPECT_EQ(result, Err(SnowflakeError::kConfigurationReadFailed));
    EXPECT_TRUE(RegistryAbsent(layout.State()));
  }
}

void InvalidStateDirectory(std::string_view name) {
  SCOPED_TRACE(std::string(name));
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("state-invalid"));
  std::string value;
  if (name == "Empty") {
    value.clear();
  } else if (name == "Relative") {
    // Exists relative to the child's working directory: only an absolute
    // path is acceptable even when the relative one would resolve.
    std::error_code ec;
    ASSERT_TRUE(fs::create_directory(layout.Work() / "relative-state", ec)) << ec.message();
    value = "relative-state";
  } else if (name == "Missing") {
    value = bedrock::test::PathToUtf8(layout.Root() / "missing-state");
  } else {
    // 4096 bytes whose prefixes ending in a separator or "." still name the
    // existing state directory: using a truncated buffer would succeed.
#if defined(_WIN32)
    const std::string step = "\\.";
#else
    const std::string step = "/.";
#endif
    value = layout.StateText();
    while (value.size() + step.size() <= 4096) value += step;
    while (value.size() < 4096) value += step.substr(0, 1);
    ASSERT_EQ(value.size(), 4096u);
  }
  std::int64_t result = 0;
  ASSERT_TRUE(Probe(layout, std::string("state-invalid-") + std::string(name), Present(value),
                    Present("3"), result));
  EXPECT_EQ(result, Err(SnowflakeError::kInvalidStatePath));
  EXPECT_TRUE(RegistryAbsent(layout.State()));
  EXPECT_TRUE(RegistryAbsent(layout.Work()));
  EXPECT_TRUE(RegistryAbsent(layout.Work() / "relative-state"));
}

const bool kStateMatrixRegistered = [] {
  for (const char* name : {"Empty", "Relative", "Missing", "Oversized"})
    Register<SnowflakeConfigProcess>("SnowflakeConfigProcess",
                                     std::string("InvalidStateDirectory/") + name,
                                     [name] { InvalidStateDirectory(name); });
  return true;
}();

// First-load cache: the child generates once, the parent changes files, the
// child changes its environment and generates on the same and a new thread.
struct CacheResults {
  std::int64_t first = 0, second = 0, thread = 0;
};

AssertionResult RunCacheProbe(const CaseLayout& layout, std::string_view case_id,
                              ChildEnvironmentValue initial_machine,
                              const std::function<AssertionResult()>& change_files,
                              CacheResults& results) {
  const fs::path first_report = layout.Report(std::string(case_id) + "-first");
  const fs::path final_report = layout.Report(std::string(case_id) + "-final");
  const fs::path continue_file = layout.Root() / "continue";
  SnowflakeChildSpec spec;
  if (auto made = MakeSpec(layout, "probe-cache", first_report, case_id,
                           {bedrock::test::PathToUtf8(continue_file),
                            bedrock::test::PathToUtf8(final_report),
                            bedrock::test::PathToUtf8(layout.StateB()), "6"},
                           Present(layout.StateText()), std::move(initial_machine), spec);
      !made)
    return made;
  std::string diagnostic;
  SnowflakeChild child;
  if (!bedrock::test::StartSnowflakeChild(spec, child, diagnostic))
    return AssertionFailure() << diagnostic;
  if (!bedrock::test::WaitForFileWhileChildRuns(child, first_report,
                                                bedrock::test::kDefaultChildTimeoutMs, diagnostic))
    return AssertionFailure() << diagnostic;
  ChildReport first;
  if (!bedrock::test::ReadChildReport(first_report, case_id, {"first_result"}, first, diagnostic) ||
      !first.Int64("first_result", results.first, diagnostic))
    return AssertionFailure() << diagnostic;
  if (auto changed = change_files(); !changed) return changed;
  if (!bedrock::test::WriteFileBytes(continue_file, "continue", diagnostic))
    return AssertionFailure() << diagnostic;
  int exit_code = -1;
  if (!child.Wait(bedrock::test::kDefaultChildTimeoutMs, exit_code, diagnostic))
    return AssertionFailure() << diagnostic;
  if (exit_code != 0) return AssertionFailure() << "probe-cache exit code " << exit_code;
  ChildReport final_values;
  std::int64_t first_again = 0;
  if (!bedrock::test::ReadChildReport(final_report, case_id,
                                      {"first_result", "second_result", "thread_result"},
                                      final_values, diagnostic) ||
      !final_values.Int64("first_result", first_again, diagnostic) ||
      !final_values.Int64("second_result", results.second, diagnostic) ||
      !final_values.Int64("thread_result", results.thread, diagnostic))
    return AssertionFailure() << diagnostic;
  if (first_again != results.first)
    return AssertionFailure() << "first_result changed between reports";
  Note(case_id, "first_result", results.first);
  Note(case_id, "second_result", results.second);
  Note(case_id, "thread_result", results.thread);
  return AssertionSuccess();
}

TEST_F(SnowflakeConfigProcess, SuccessfulConfigurationIsImmutableAcrossThreads) {
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("cache-success"));
  ASSERT_TRUE(WriteMachineFile(layout.State(), "3"));
  CacheResults results;
  ASSERT_TRUE(RunCacheProbe(layout, "cache-success", Absent(), [&]() -> AssertionResult {
    if (auto a = WriteMachineFile(layout.State(), "4"); !a) return a;
    return WriteMachineFile(layout.StateB(), "6");
  }, results));
  ASSERT_GE(results.first, 0);
  ASSERT_GE(results.second, 0);
  ASSERT_GE(results.thread, 0);
  EXPECT_EQ(MachineOf(results.first), 3);
  EXPECT_EQ(MachineOf(results.second), 3);
  EXPECT_EQ(MachineOf(results.thread), 3);
  std::string registry;
  ASSERT_TRUE(ReadExact(layout.State() / "snowflake.registry", registry));
  ASSERT_NO_FATAL_FAILURE(ExpectFreshRegistry(registry, 3));
  EXPECT_TRUE(RegistryAbsent(layout.StateB()));
}

TEST_F(SnowflakeConfigProcess, MissingConfigurationFailureIsImmutable) {
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("cache-missing"));
  CacheResults results;
  ASSERT_TRUE(RunCacheProbe(layout, "cache-missing", Absent(), [&]() -> AssertionResult {
    if (auto a = WriteMachineFile(layout.State(), "3"); !a) return a;
    return WriteMachineFile(layout.StateB(), "6");
  }, results));
  EXPECT_EQ(results.first, Err(SnowflakeError::kMissingMachineId));
  EXPECT_EQ(results.second, Err(SnowflakeError::kMissingMachineId));
  EXPECT_EQ(results.thread, Err(SnowflakeError::kMissingMachineId));
  EXPECT_TRUE(RegistryAbsent(layout.State()));
  EXPECT_TRUE(RegistryAbsent(layout.StateB()));
}

TEST_F(SnowflakeConfigProcess, InvalidConfigurationFailureIsImmutable) {
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("cache-invalid"));
  ASSERT_TRUE(WriteMachineFile(layout.State(), "3"));
  CacheResults results;
  ASSERT_TRUE(RunCacheProbe(layout, "cache-invalid", Present("8"), [&]() -> AssertionResult {
    return WriteMachineFile(layout.StateB(), "6");
  }, results));
  EXPECT_EQ(results.first, Err(SnowflakeError::kInvalidMachineId));
  EXPECT_EQ(results.second, Err(SnowflakeError::kInvalidMachineId));
  EXPECT_EQ(results.thread, Err(SnowflakeError::kInvalidMachineId));
  EXPECT_TRUE(RegistryAbsent(layout.State()));
  EXPECT_TRUE(RegistryAbsent(layout.StateB()));
}

TEST_F(SnowflakeConfigProcess, NewProcessSeesChangedConfiguration) {
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("cache-new-process"));
  ASSERT_TRUE(WriteMachineFile(layout.State(), "3"));
  CacheResults results;
  ASSERT_TRUE(RunCacheProbe(layout, "cache-before-exec", Absent(), [&]() -> AssertionResult {
    return WriteMachineFile(layout.StateB(), "6");
  }, results));
  ASSERT_GE(results.second, 0);
  EXPECT_EQ(MachineOf(results.second), 3);
  // The configuration the previous process ignored is valid for a new exec.
  std::int64_t result = -99;
  ASSERT_TRUE(Probe(layout, "cache-after-exec", Present(bedrock::test::PathToUtf8(layout.StateB())),
                    Present("6"), result));
  ASSERT_GE(result, 0);
  EXPECT_EQ(MachineOf(result), 6);
  std::string registry;
  ASSERT_TRUE(ReadExact(layout.StateB() / "snowflake.registry", registry));
  ASSERT_NO_FATAL_FAILURE(ExpectFreshRegistry(registry, 6));
}

// ---------------------------------------------------------------------------
// SnowflakeRegistryProcess: fail-closed registry validation.
// ---------------------------------------------------------------------------
class SnowflakeRegistryProcess : public ::testing::Test {};

AssertionResult SeedRegistry(const CaseLayout& layout, std::string& bytes) {
  if (auto written = WriteMachineFile(layout.State(), "3"); !written) return written;
  std::int64_t seeded = 0;
  if (auto probed = Probe(layout, "seed", Present(layout.StateText()), Absent(), seeded,
                          "registry-seed");
      !probed)
    return probed;
  if (seeded < 0) return AssertionFailure() << "registry-seed returned " << seeded;
  if (auto read = ReadExact(layout.State() / "snowflake.registry", bytes); !read) return read;
  if (bytes.size() != kRegistrySize || static_cast<unsigned char>(bytes[28]) != 3u)
    return AssertionFailure() << "seeded registry is not a 192-byte machine-3 record";
  return AssertionSuccess();
}

void ExpectRegistryRejected(const std::string& name,
                            const std::function<void(std::string&)>& mutate,
                            std::int64_t expected) {
  SCOPED_TRACE(name);
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("registry"));
  std::string seeded;
  ASSERT_TRUE(SeedRegistry(layout, seeded));
  std::string mutated = seeded;
  mutate(mutated);
  const fs::path registry = layout.State() / "snowflake.registry";
  ASSERT_TRUE(WriteExact(registry, mutated));
  std::string before;
  ASSERT_TRUE(ReadExact(registry, before));
  std::int64_t result = 0;
  ASSERT_TRUE(Probe(layout, "check", Present(layout.StateText()), Absent(), result));
  EXPECT_EQ(result, expected);
  std::string after;
  ASSERT_TRUE(ReadExact(registry, after));
  EXPECT_EQ(after.size(), before.size());
  EXPECT_TRUE(after == before) << "registry bytes changed after a rejected open";
}

const bool kRegistryMatrixRegistered = [] {
  const auto corrupt = Err(SnowflakeError::kRegistryCorrupt);
  const auto incompatible = Err(SnowflakeError::kRegistryIncompatible);
  const std::pair<const char*, std::size_t> sizes[] = {
      {"Zero", 0}, {"One", 1}, {"HeaderOnly", 64}, {"Truncated", 191}, {"Extra", 193}};
  for (const auto& [name, size] : sizes) {
    const std::string test = std::string("RejectsWrongSize/") + name;
    const std::size_t length = size;
    Register<SnowflakeRegistryProcess>("SnowflakeRegistryProcess", test, [test, length, corrupt] {
      ExpectRegistryRejected(test, [length](std::string& bytes) { bytes.resize(length, '\0'); },
                             corrupt);
    });
  }
  struct Field {
    const char* name;
    std::size_t offset, width;
    std::uint64_t value;
  };
  const Field fields[] = {
      {"Version", 8, 2, 2},       {"HeaderSize", 10, 2, 63},
      {"RecordSize", 12, 4, 193}, {"Epoch", 16, 8, kEpochMs + 1},
      {"TimestampBits", 24, 1, 40}, {"MachineBits", 25, 1, 4},
      {"SlotBits", 26, 1, 6},     {"SequenceBits", 27, 1, 11},
  };
  for (const Field& field : fields) {
    const std::string test = std::string("RejectsIncompatibleMetadata/") + field.name;
    Register<SnowflakeRegistryProcess>("SnowflakeRegistryProcess", test,
                                       [test, field, incompatible] {
      ExpectRegistryRejected(test, [field](std::string& bytes) {
        PutLittleEndian(bytes, field.offset, field.width, field.value);
      }, incompatible);
    });
  }
  const std::pair<const char*, unsigned char> machines[] = {{"Eight", 8}, {"MaxByte", 255}};
  for (const auto& [name, value] : machines) {
    const std::string test = std::string("RejectsImpossibleMachine/") + name;
    const unsigned char byte = value;
    Register<SnowflakeRegistryProcess>("SnowflakeRegistryProcess", test, [test, byte, corrupt] {
      ExpectRegistryRejected(test, [byte](std::string& bytes) { bytes[28] = static_cast<char>(byte); },
                             corrupt);
    });
  }
  for (std::size_t offset = 29; offset < kRegistrySize; ++offset) {
    const std::string test = "RejectsNonZeroReservedByte/Offset" + std::to_string(offset);
    Register<SnowflakeRegistryProcess>("SnowflakeRegistryProcess", test, [test, offset, corrupt] {
      ExpectRegistryRejected(test, [offset](std::string& bytes) { bytes[offset] = '\x01'; },
                             corrupt);
    });
  }
  return true;
}();

TEST_F(SnowflakeRegistryProcess, RejectsWrongMagic) {
  ExpectRegistryRejected("RejectsWrongMagic", [](std::string& bytes) { bytes[0] = 'X'; },
                         Err(SnowflakeError::kRegistryCorrupt));
}

TEST_F(SnowflakeRegistryProcess, RejectsValidDifferentMachine) {
  ExpectRegistryRejected("RejectsValidDifferentMachine",
                         [](std::string& bytes) { bytes[28] = '\x04'; },
                         Err(SnowflakeError::kMachineIdMismatch));
}

TEST_F(SnowflakeRegistryProcess, ValidRecordIsAcceptedWithoutMutation) {
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("registry-valid"));
  std::string before;
  ASSERT_TRUE(SeedRegistry(layout, before));
  std::int64_t result = -99;
  ASSERT_TRUE(Probe(layout, "check", Present(layout.StateText()), Absent(), result));
  ASSERT_GE(result, 0);
  EXPECT_EQ(MachineOf(result), 3);
  std::string after;
  ASSERT_TRUE(ReadExact(layout.State() / "snowflake.registry", after));
  EXPECT_TRUE(after == before);
}

TEST_F(SnowflakeRegistryProcess, MissingRecordIsCreatedOnce) {
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("registry-create"));
  ASSERT_TRUE(WriteMachineFile(layout.State(), "3"));
  ASSERT_TRUE(RegistryAbsent(layout.State()));
  const fs::path registry = layout.State() / "snowflake.registry";
  const fs::path ready = layout.Report("holder");
  const fs::path release = layout.Root() / "release";
  SnowflakeChildSpec spec;
  ASSERT_TRUE(MakeSpec(layout, "hold", ready, "holder", {bedrock::test::PathToUtf8(release)},
                       Present(layout.StateText()), Absent(), spec));
  std::string diagnostic;
  SnowflakeChild holder;
  ASSERT_TRUE(bedrock::test::StartSnowflakeChild(spec, holder, diagnostic)) << diagnostic;
  ASSERT_TRUE(bedrock::test::WaitForFileWhileChildRuns(holder, ready,
                                                       bedrock::test::kDefaultChildTimeoutMs,
                                                       diagnostic))
      << diagnostic;
  ChildReport report;
  std::int64_t held = -99;
  ASSERT_TRUE(bedrock::test::ReadChildReport(ready, "holder", {"result"}, report, diagnostic) &&
              report.Int64("result", held, diagnostic))
      << diagnostic;
  ASSERT_GE(held, 0);
  EXPECT_EQ(MachineOf(held), 3);
  // While the live slot lock is held beyond EOF, the record stays 192 bytes.
  std::string created;
  ASSERT_TRUE(ReadExact(registry, created));
  ASSERT_NO_FATAL_FAILURE(ExpectFreshRegistry(created, 3));
  std::int64_t second = -99;
  ASSERT_TRUE(Probe(layout, "second-reader", Present(layout.StateText()), Absent(), second));
  ASSERT_GE(second, 0);
  EXPECT_EQ(MachineOf(second), 3);
  EXPECT_NE(SlotOf(second), SlotOf(held));
  std::string reread;
  ASSERT_TRUE(ReadExact(registry, reread));
  EXPECT_TRUE(reread == created) << "an existing record must not be re-created";
  ASSERT_TRUE(bedrock::test::WriteFileBytes(release, "release", diagnostic)) << diagnostic;
  int exit_code = -1;
  ASSERT_TRUE(holder.Wait(bedrock::test::kDefaultChildTimeoutMs, exit_code, diagnostic)) << diagnostic;
  EXPECT_EQ(exit_code, 0);
  ASSERT_TRUE(ReadExact(registry, reread));
  EXPECT_TRUE(reread == created);
}

// ---------------------------------------------------------------------------
// SnowflakeProcess: ownership across processes, threads and the test fixture.
// ---------------------------------------------------------------------------
AssertionResult ParseIds(const std::string& text, std::vector<std::int64_t>& ids) {
  std::size_t start = 0;
  while (start < text.size()) {
    const std::size_t end = text.find('\n', start);
    if (end == std::string::npos) return AssertionFailure() << "unterminated ID line";
    std::int64_t id = 0;
    const auto parsed = std::from_chars(text.data() + start, text.data() + end, id);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + end)
      return AssertionFailure() << "malformed ID line";
    ids.push_back(id);
    start = end + 1;
  }
  return AssertionSuccess();
}

TEST(SnowflakeProcess, SubprocessStreamsAreUniqueAndDecodeConfiguredMachine) {
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("streams"));
  ASSERT_TRUE(WriteMachineFile(layout.State(), "3"));
  constexpr int kChildren = 4;
  constexpr int kPerChild = 250;
  std::vector<SnowflakeChild> children(kChildren);
  std::vector<fs::path> reports, outputs;
  std::string diagnostic;
  for (int i = 0; i < kChildren; ++i) {
    reports.push_back(layout.Report("stream-" + std::to_string(i)));
    outputs.push_back(layout.Root() / ("ids-" + std::to_string(i) + ".txt"));
    SnowflakeChildSpec spec;
    ASSERT_TRUE(MakeSpec(layout, "generate", reports.back(), "stream-" + std::to_string(i),
                         {std::to_string(kPerChild), bedrock::test::PathToUtf8(outputs.back())},
                         Present(layout.StateText()), Absent(), spec));
    ASSERT_TRUE(bedrock::test::StartSnowflakeChild(spec, children[static_cast<std::size_t>(i)],
                                                   diagnostic))
        << diagnostic;
  }
  for (auto& child : children) {
    int exit_code = -1;
    ASSERT_TRUE(child.Wait(bedrock::test::kDefaultChildTimeoutMs, exit_code, diagnostic)) << diagnostic;
    EXPECT_EQ(exit_code, 0);
  }
  std::set<std::int64_t> unique;
  std::size_t total = 0;
  for (int i = 0; i < kChildren; ++i) {
    SCOPED_TRACE("child " + std::to_string(i));
    ChildReport report;
    std::int64_t requested = 0, generated = 0, last = -99;
    ASSERT_TRUE(bedrock::test::ReadChildReport(reports[static_cast<std::size_t>(i)],
                                               "stream-" + std::to_string(i),
                                               {"requested", "generated", "last_result"}, report,
                                               diagnostic) &&
                report.Int64("requested", requested, diagnostic) &&
                report.Int64("generated", generated, diagnostic) &&
                report.Int64("last_result", last, diagnostic))
        << diagnostic;
    EXPECT_EQ(requested, kPerChild);
    ASSERT_GE(last, 0) << "generation error, no retry";
    EXPECT_EQ(generated, kPerChild);
    std::string text;
    ASSERT_TRUE(ReadExact(outputs[static_cast<std::size_t>(i)], text));
    std::vector<std::int64_t> ids;
    ASSERT_TRUE(ParseIds(text, ids));
    ASSERT_EQ(ids.size(), static_cast<std::size_t>(kPerChild));
    const int slot = SlotOf(ids.front());
    for (const auto id : ids) {
      ASSERT_GE(id, 0);
      EXPECT_EQ(MachineOf(id), 3);
      EXPECT_EQ(SlotOf(id), slot) << "one issuing thread keeps one slot";
      unique.insert(id);
    }
    total += ids.size();
  }
  EXPECT_EQ(total, static_cast<std::size_t>(kChildren * kPerChild));
  EXPECT_EQ(unique.size(), total);
}

AssertionResult StartHolder(const CaseLayout& layout, const std::string& case_id,
                            const fs::path& release, SnowflakeChild& child) {
  SnowflakeChildSpec spec;
  if (auto made = MakeSpec(layout, "hold", layout.Report(case_id), case_id,
                           {bedrock::test::PathToUtf8(release)}, Present(layout.StateText()),
                           Absent(), spec);
      !made)
    return made;
  std::string diagnostic;
  if (!bedrock::test::StartSnowflakeChild(spec, child, diagnostic))
    return AssertionFailure() << diagnostic;
  return AssertionSuccess();
}

AssertionResult HolderResult(const CaseLayout& layout, const std::string& case_id,
                             SnowflakeChild& child, std::uint32_t timeout_ms,
                             std::int64_t& result) {
  std::string diagnostic;
  const fs::path report = layout.Report(case_id);
  if (!bedrock::test::WaitForFileWhileChildRuns(child, report, timeout_ms, diagnostic))
    return AssertionFailure() << diagnostic;
  ChildReport parsed;
  if (!bedrock::test::ReadChildReport(report, case_id, {"result"}, parsed, diagnostic) ||
      !parsed.Int64("result", result, diagnostic))
    return AssertionFailure() << diagnostic;
  return AssertionSuccess();
}

TEST(SnowflakeProcess, KilledOwnerReleasesKernelSlotAfterWait) {
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("kill"));
  ASSERT_TRUE(WriteMachineFile(layout.State(), "3"));
  const fs::path release = layout.Root() / "release";
  SnowflakeChild owner;
  ASSERT_TRUE(StartHolder(layout, "owner", release, owner));
  std::int64_t held = -99;
  ASSERT_TRUE(HolderResult(layout, "owner", owner, bedrock::test::kDefaultChildTimeoutMs, held));
  ASSERT_GE(held, 0);
  std::string diagnostic;
  ASSERT_TRUE(owner.TerminateAndWait(diagnostic)) << diagnostic;
  ASSERT_FALSE(owner.Owned());
  std::int64_t replacement = -99;
  ASSERT_TRUE(Probe(layout, "replacement", Present(layout.StateText()), Absent(), replacement));
  ASSERT_GE(replacement, 0);
  EXPECT_EQ(MachineOf(replacement), 3);
  // The lowest free slot is taken: equality proves the kernel released the
  // killed owner's lock instead of the replacement moving to another slot.
  EXPECT_EQ(SlotOf(replacement), SlotOf(held));
}

TEST(SnowflakeProcess, Exactly128ConcurrentOwnersAnd129thExhaustion) {
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("capacity"));
  ASSERT_TRUE(WriteMachineFile(layout.State(), "3"));
  const fs::path release = layout.Root() / "release";
  std::vector<SnowflakeChild> owners(128);
  // Declared after the owners: on any failure the owners are released before
  // their destructors terminate and reap whatever is still running.
  struct ReleaseOnExit {
    const fs::path& path;
    ~ReleaseOnExit() {
      std::string ignored;
      (void)bedrock::test::WriteFileBytes(path, "release", ignored);
    }
  } release_guard{release};
  for (std::size_t i = 0; i < owners.size(); ++i)
    ASSERT_TRUE(StartHolder(layout, "owner-" + std::to_string(i), release, owners[i]))
        << "failed to launch owner " << i;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
  std::set<int> slots;
  for (std::size_t i = 0; i < owners.size(); ++i) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    const auto budget = static_cast<std::uint32_t>(
        std::max<std::int64_t>(static_cast<std::int64_t>(left.count()), 1));
    std::int64_t held = -99;
    ASSERT_TRUE(HolderResult(layout, "owner-" + std::to_string(i), owners[i], budget, held))
        << "owner " << i << " did not reach the barrier";
    ASSERT_GE(held, 0) << "owner " << i;
    EXPECT_EQ(MachineOf(held), 3);
    slots.insert(SlotOf(held));
  }
  EXPECT_EQ(slots.size(), 128u);
  std::int64_t overflow = 0;
  ASSERT_TRUE(Probe(layout, "overflow", Present(layout.StateText()), Absent(), overflow));
  EXPECT_EQ(overflow, Err(SnowflakeError::kSlotsExhausted));
  std::string diagnostic;
  ASSERT_TRUE(bedrock::test::WriteFileBytes(release, "release", diagnostic)) << diagnostic;
  for (std::size_t i = 0; i < owners.size(); ++i) {
    int exit_code = -1;
    ASSERT_TRUE(owners[i].Wait(bedrock::test::kDefaultChildTimeoutMs, exit_code, diagnostic))
        << "owner " << i << ": " << diagnostic;
    EXPECT_EQ(exit_code, 0) << "owner " << i;
  }
}

TEST(SnowflakeProcess, MoreThan128SequentialThreadLifetimesReuseSlots) {
  std::vector<std::int64_t> ids;
  for (int i = 0; i < 140; ++i) {
    std::int64_t value = -99;
    std::thread issuer([&] { value = bedrock::Snowflake::Generate(); });
    issuer.join();
    ASSERT_GE(value, 0) << "thread " << i;
    EXPECT_EQ(MachineOf(value), 0) << "test fixture machine id";
    ids.push_back(value);
  }
  EXPECT_EQ(ids.size(), 140u);
  const std::set<std::int64_t> unique(ids.begin(), ids.end());
  EXPECT_EQ(unique.size(), ids.size());
}

// The default test fixture restores absent and present-empty variables.
AssertionResult RunFixtureProbe(const CaseLayout& layout, const std::string& case_id,
                                ChildEnvironmentValue state, ChildEnvironmentValue machine,
                                ChildReport& report) {
  SnowflakeChildSpec spec;
  const fs::path path = layout.Report(case_id);
  if (auto made = MakeSpec(layout, "probe-fixture", path, case_id, {}, std::move(state),
                           std::move(machine), spec);
      !made)
    return made;
  std::string diagnostic;
  if (!bedrock::test::RunSnowflakeHelper(
          spec, path, case_id,
          {"state_present_before", "machine_present_before", "fixture_ok",
           "state_matches_during", "machine_during_is_3", "machine_file_is_3",
           "state_present_after", "state_value_after", "machine_present_after",
           "machine_value_after", "directory_removed"},
          report, diagnostic))
    return AssertionFailure() << diagnostic;
  return AssertionSuccess();
}

// Non-fatal on purpose: callers check several independent report fields.
void ExpectFlag(const ChildReport& report, const char* key, std::uint64_t expected) {
  std::uint64_t value = 99;
  std::string diagnostic;
  if (!report.UInt64(key, value, diagnostic)) {
    ADD_FAILURE() << diagnostic;
    return;
  }
  EXPECT_EQ(value, expected) << key;
}

TEST(SnowflakeProcess, FixtureRestoresAbsentAndPresentEmptyEnvironment) {
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("fixture"));
  {
    SCOPED_TRACE("state present-empty, machine absent");
    ChildReport report;
    ASSERT_TRUE(RunFixtureProbe(layout, "fixture-empty-state", Present(""), Absent(), report));
    ExpectFlag(report, "state_present_before", 1);
    ExpectFlag(report, "machine_present_before", 0);
    ExpectFlag(report, "fixture_ok", 1);
    ExpectFlag(report, "state_matches_during", 1);
    ExpectFlag(report, "machine_during_is_3", 1);
    ExpectFlag(report, "machine_file_is_3", 1);
    ExpectFlag(report, "state_present_after", 1);
    EXPECT_EQ(*report.Find("state_value_after"), "");
    ExpectFlag(report, "machine_present_after", 0);
    ExpectFlag(report, "directory_removed", 1);
  }
  {
    SCOPED_TRACE("state absent, machine present");
    ChildReport report;
    ASSERT_TRUE(RunFixtureProbe(layout, "fixture-machine-7", Absent(), Present("7"), report));
    ExpectFlag(report, "state_present_before", 0);
    ExpectFlag(report, "machine_present_before", 1);
    ExpectFlag(report, "fixture_ok", 1);
    ExpectFlag(report, "state_present_after", 0);
    ExpectFlag(report, "machine_present_after", 1);
    EXPECT_EQ(*report.Find("machine_value_after"), "7");
    ExpectFlag(report, "directory_removed", 1);
  }
}

// probe-archive self-check on the internal clone; the DLL boundary is covered
// by the external SnowflakeApiProcess suite.
AssertionResult RunArchiveProbe(const CaseLayout& layout, const std::string& case_id,
                                const char* submode, ChildEnvironmentValue machine,
                                std::initializer_list<std::string_view> keys,
                                ChildReport& report) {
  SnowflakeChildSpec spec;
  const fs::path path = layout.Report(case_id);
  if (auto made = MakeSpec(layout, "probe-archive", path, case_id, {submode},
                           Present(layout.StateText()), std::move(machine), spec);
      !made)
    return made;
  std::string diagnostic;
  if (!bedrock::test::RunSnowflakeHelper(spec, path, case_id, keys, report, diagnostic))
    return AssertionFailure() << diagnostic;
  return AssertionSuccess();
}

TEST(SnowflakeProcess, MissingConfigMapsToArchiveStatusAndDumpDoesNotWrite) {
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("archive-missing"));
  constexpr std::uint64_t kMissing = 0x101;
  {
    ChildReport report;
    ASSERT_TRUE(RunArchiveProbe(layout, "dump", "dump", Absent(),
                                {"dump_code", "dump_get_status", "dump_failed", "output_size"},
                                report));
    ExpectFlag(report, "dump_code", kMissing);
    ExpectFlag(report, "dump_get_status", kMissing);
    ExpectFlag(report, "dump_failed", 1);
    ExpectFlag(report, "output_size", 0);
  }
  {
    ChildReport report;
    ASSERT_TRUE(RunArchiveProbe(layout, "initialize", "initialize", Absent(),
                                {"initialize_code", "initialize_get_status", "initialize_failed"},
                                report));
    ExpectFlag(report, "initialize_code", kMissing);
    ExpectFlag(report, "initialize_get_status", kMissing);
    ExpectFlag(report, "initialize_failed", 1);
  }
  {
    ChildReport report;
    ASSERT_TRUE(RunArchiveProbe(layout, "load", "load-after-failure", Absent(),
                                {"initialize_code", "initialize_get_status", "load_code",
                                 "load_get_status", "destination_value", "accept_calls"},
                                report));
    ExpectFlag(report, "initialize_code", kMissing);
    ExpectFlag(report, "load_code", kMissing);
    ExpectFlag(report, "load_get_status", kMissing);
    ExpectFlag(report, "destination_value", 99);
    ExpectFlag(report, "accept_calls", 0);
  }
  EXPECT_TRUE(RegistryAbsent(layout.State()));
}

TEST(SnowflakeProcess, ArchiveProbeReportsValidConfigurationOutput) {
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("archive-valid"));
  {
    ChildReport report;
    ASSERT_TRUE(RunArchiveProbe(layout, "dump", "dump", Present("3"),
                                {"dump_code", "dump_get_status", "dump_failed", "output_size"},
                                report));
    ExpectFlag(report, "dump_code", 0);
    ExpectFlag(report, "dump_get_status", 0);
    ExpectFlag(report, "dump_failed", 0);
    std::uint64_t size = 0;
    std::string diagnostic;
    ASSERT_TRUE(report.UInt64("output_size", size, diagnostic)) << diagnostic;
    EXPECT_GT(size, 0u);
  }
  {
    ChildReport report;
    ASSERT_TRUE(RunArchiveProbe(layout, "load", "load-after-failure", Present("3"),
                                {"initialize_code", "load_code", "load_get_status",
                                 "destination_value", "accept_calls"},
                                report));
    ExpectFlag(report, "initialize_code", 0);
    ExpectFlag(report, "load_code", 0);
    ExpectFlag(report, "load_get_status", 0);
    ExpectFlag(report, "destination_value", 1);
    ExpectFlag(report, "accept_calls", 1);
  }
}

// ---------------------------------------------------------------------------
// SnowflakeProcessLauncher: the launcher itself.
// ---------------------------------------------------------------------------
TEST(SnowflakeProcessLauncher, InvalidUtf8IsRejectedBeforeCreateProcess) {
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("launcher-utf8"));
  const fs::path report = layout.Report("launcher-utf8");
  SnowflakeChildSpec valid;
  ASSERT_TRUE(MakeSpec(layout, "probe-config", report, "launcher-utf8", {},
                       Present(layout.StateText()), Present("3"), valid));
  struct Invalid {
    const char* name;
    std::function<void(SnowflakeChildSpec&)> apply;
  };
  const Invalid cases[] = {
      {"argument", [](SnowflakeChildSpec& s) { s.arguments.push_back("\xC3\x28"); }},
      {"argument-nul", [](SnowflakeChildSpec& s) { s.arguments.push_back(std::string("a\0b", 3)); }},
      {"machine", [](SnowflakeChildSpec& s) { s.machine_id = Present("\xFF"); }},
      {"state-surrogate", [](SnowflakeChildSpec& s) { s.state_directory = Present("\xED\xA0\x80"); }},
      {"state-overlong", [](SnowflakeChildSpec& s) { s.state_directory = Present("\xC0\xAF"); }},
  };
  for (const auto& invalid : cases) {
    SCOPED_TRACE(invalid.name);
    SnowflakeChildSpec spec = valid;
    invalid.apply(spec);
    const auto before = bedrock::test::SnowflakeChildLaunchAttemptsForTesting();
    SnowflakeChild child;
    std::string diagnostic;
    EXPECT_FALSE(bedrock::test::StartSnowflakeChild(spec, child, diagnostic));
    EXPECT_FALSE(diagnostic.empty());
    EXPECT_FALSE(child.Owned());
    EXPECT_EQ(bedrock::test::SnowflakeChildLaunchAttemptsForTesting(), before)
        << "no process creation call for invalid input";
  }
  EXPECT_TRUE(bedrock::test::IsValidUtf8(""));
  EXPECT_FALSE(bedrock::test::IsValidUtf8("\xC0\xAF"));
#if defined(_WIN32)
  std::wstring wide = L"sentinel";
  EXPECT_TRUE(bedrock::test::Utf8ToWide("", wide)) << "empty is a value, not a failure";
  EXPECT_TRUE(wide.empty());
  EXPECT_FALSE(bedrock::test::Utf8ToWide("\xFF", wide));
  EXPECT_TRUE(bedrock::test::Utf8ToWide("\xEC\xBC\x80\xF0\x9F\x98\x80", wide));
  EXPECT_EQ(wide, std::wstring(L"\xCF00\xD83D\xDE00"));
#endif
  // Control: a present-empty machine value is valid launcher input; the child
  // runs and the library rejects the empty value itself.
  SnowflakeChildSpec empty_machine = valid;
  empty_machine.machine_id = Present("");
  const auto before = bedrock::test::SnowflakeChildLaunchAttemptsForTesting();
  ChildReport parsed;
  std::string diagnostic;
  ASSERT_TRUE(bedrock::test::RunSnowflakeHelper(empty_machine, report, "launcher-utf8", {"result"},
                                                parsed, diagnostic))
      << diagnostic;
  EXPECT_EQ(bedrock::test::SnowflakeChildLaunchAttemptsForTesting(), before + 1);
  std::int64_t result = 0;
  ASSERT_TRUE(parsed.Int64("result", result, diagnostic)) << diagnostic;
  EXPECT_EQ(result, Err(SnowflakeError::kInvalidMachineId));
}

TEST(SnowflakeProcessLauncher, EmptyExecutableIsRejected) {
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("launcher-exe"));
  SnowflakeChildSpec valid;
  ASSERT_TRUE(MakeSpec(layout, "probe-config", layout.Report("exe"), "exe", {},
                       Present(layout.StateText()), Present("3"), valid));
  for (const char* which : {"empty-executable", "relative-executable", "empty-working-directory"}) {
    SCOPED_TRACE(which);
    SnowflakeChildSpec spec = valid;
    if (std::string_view(which) == "empty-executable") spec.executable.clear();
    if (std::string_view(which) == "relative-executable") spec.executable = valid.executable.filename();
    if (std::string_view(which) == "empty-working-directory") spec.working_directory.clear();
    const auto before = bedrock::test::SnowflakeChildLaunchAttemptsForTesting();
    SnowflakeChild child;
    std::string diagnostic;
    EXPECT_FALSE(bedrock::test::StartSnowflakeChild(spec, child, diagnostic));
    EXPECT_FALSE(diagnostic.empty());
    EXPECT_FALSE(child.Owned());
    EXPECT_EQ(bedrock::test::SnowflakeChildLaunchAttemptsForTesting(), before);
  }
  // A missing executable reaches the OS call and fails fast without a child.
  SnowflakeChildSpec missing = valid;
  missing.executable = layout.Root() / "missing-executable";
  const auto before = bedrock::test::SnowflakeChildLaunchAttemptsForTesting();
  SnowflakeChild child;
  std::string diagnostic;
  EXPECT_FALSE(bedrock::test::StartSnowflakeChild(missing, child, diagnostic));
  EXPECT_FALSE(diagnostic.empty());
  EXPECT_FALSE(child.Owned());
  EXPECT_EQ(bedrock::test::SnowflakeChildLaunchAttemptsForTesting(), before + 1);
}

TEST(SnowflakeProcessLauncher, UnicodePathAndArgumentRoundTrip) {
  // Hangul is not representable in the Japanese ANSI code page (cp932), so any
  // ACP round trip of the path or argument would corrupt it.
  bedrock::test::ScopedTestDirectory root;
  std::string diagnostic;
  ASSERT_TRUE(root.Create("유니코드 경로 テスト 😀", diagnostic)) << diagnostic;
  const fs::path work = root.Path() / bedrock::test::PathFromUtf8("작업 dir");
  const fs::path state = root.Path() / bedrock::test::PathFromUtf8("상태 state ☃");
  std::error_code ec;
  ASSERT_TRUE(fs::create_directory(work, ec)) << ec.message();
  ASSERT_TRUE(fs::create_directory(state, ec)) << ec.message();
  ASSERT_TRUE(WriteMachineFile(state, "5"));
  const fs::path report = root.Path() / bedrock::test::PathFromUtf8("보고서 report.txt");
  const std::string case_id = "케이스 ケース 😀 \"quoted\" back\\slash\\ id";
  SnowflakeChildSpec spec;
  ASSERT_TRUE(bedrock::test::MakeHelperSpec("probe-config", report, case_id, {}, work,
                                            Present(bedrock::test::PathToUtf8(state)), Absent(),
                                            spec, diagnostic))
      << diagnostic;
  ChildReport parsed;
  ASSERT_TRUE(bedrock::test::RunSnowflakeHelper(spec, report, case_id, {"result"}, parsed,
                                                diagnostic))
      << diagnostic;
  std::int64_t result = -99;
  ASSERT_TRUE(parsed.Int64("result", result, diagnostic)) << diagnostic;
  ASSERT_GE(result, 0);
  EXPECT_EQ(MachineOf(result), 5);
  std::string registry;
  ASSERT_TRUE(ReadExact(state / "snowflake.registry", registry));
  ASSERT_NO_FATAL_FAILURE(ExpectFreshRegistry(registry, 5));
}

TEST(SnowflakeProcessLauncher, ReportParserRejectsIncompleteOrAmbiguousReports) {
  CaseLayout layout;
  ASSERT_TRUE(layout.Create("report-parser"));
  const fs::path path = layout.Root() / "parser.report";
  struct Sample {
    const char* name;
    std::string bytes;
    bool valid;
  };
  const Sample samples[] = {
      {"valid", "protocol=1\ncase_id=c\nresult=-3\ncomplete=1\n", true},
      {"missing-newline", "protocol=1\ncase_id=c\nresult=-3\ncomplete=1", false},
      {"missing-complete", "protocol=1\ncase_id=c\nresult=-3\n", false},
      {"wrong-protocol", "protocol=2\ncase_id=c\nresult=-3\ncomplete=1\n", false},
      {"wrong-case", "protocol=1\ncase_id=d\nresult=-3\ncomplete=1\n", false},
      {"duplicate-key", "protocol=1\ncase_id=c\nresult=1\nresult=1\ncomplete=1\n", false},
      {"missing-required", "protocol=1\ncase_id=c\nother=1\ncomplete=1\n", false},
      {"malformed-line", "protocol=1\ncase_id=c\nresult\ncomplete=1\n", false},
      {"carriage-return", "protocol=1\ncase_id=c\nresult=1\r\ncomplete=1\n", false},
      {"empty", "", false},
  };
  for (const auto& sample : samples) {
    SCOPED_TRACE(sample.name);
    std::string diagnostic;
    ASSERT_TRUE(bedrock::test::WriteFileBytes(path, sample.bytes, diagnostic)) << diagnostic;
    ChildReport report;
    EXPECT_EQ(bedrock::test::ReadChildReport(path, "c", {"result"}, report, diagnostic),
              sample.valid)
        << diagnostic;
  }
  std::string diagnostic;
  ASSERT_TRUE(bedrock::test::WriteFileBytes(
      path, "protocol=1\ncase_id=c\nresult=-0\nbig=9223372036854775808\nlead=01\nok=-15\ncomplete=1\n",
      diagnostic));
  ChildReport report;
  ASSERT_TRUE(bedrock::test::ReadChildReport(path, "c", {"result"}, report, diagnostic)) << diagnostic;
  std::int64_t value = 0;
  EXPECT_FALSE(report.Int64("result", value, diagnostic));
  EXPECT_FALSE(report.Int64("big", value, diagnostic)) << "out of int64 range";
  EXPECT_FALSE(report.Int64("lead", value, diagnostic));
  EXPECT_FALSE(report.Int64("absent", value, diagnostic));
  ASSERT_TRUE(report.Int64("ok", value, diagnostic)) << diagnostic;
  EXPECT_EQ(value, -15);
}

}  // namespace
