// Public-boundary Snowflake tests: public Common headers and the shared
// launcher (snowflake_child.h) only.
//
// SnowflakeApi runs in this test process. SnowflakeApiProcess runs every
// library observation in a fresh child of this executable (helper mode
// probe-archive, dispatched before GoogleTest), so each primary assertion is
// the first Snowflake initialization of the real Common library in that
// process. The parent prepares the child environment and a fresh state
// directory, then compares the reported values with the public
// bedrock::ErrorCode enumerators. A child exit code only signals a harness
// failure: Archive statuses travel in the report as full-width decimals
// because a POSIX exit status keeps only 8 bits (0x101 would read as 1).
#include "common/data_types/snowflake.h"
#include "common/data_types/status.h"
#include "snowflake_child.h"

#include <gtest/gtest.h>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

TEST(SnowflakeApi, GenerateReturnsSignedValueAndWrapperRoundTrips) {
  const std::int64_t result=bedrock::Snowflake::Generate();
  ASSERT_GE(result,0);
  const bedrock::Snowflake decoded(static_cast<std::uint64_t>(result));
  EXPECT_EQ(static_cast<std::uint64_t>(decoded),static_cast<std::uint64_t>(result));
  EXPECT_LT(decoded.machine_id,8u);
  EXPECT_LT(decoded.thread_id,128u);
}

namespace {
namespace fs = std::filesystem;
using bedrock::ErrorCode;
using bedrock::test::ChildEnvironmentValue;
using bedrock::test::ChildReport;
using bedrock::test::SnowflakeChildSpec;
using ::testing::AssertionFailure;
using ::testing::AssertionResult;
using ::testing::AssertionSuccess;

constexpr std::uint64_t Raw(ErrorCode code) noexcept {
  return static_cast<std::uint32_t>(code);
}

// The exact public values this suite asserts.
static_assert(Raw(ErrorCode::kSuccess) == 0);
static_assert(Raw(ErrorCode::kSnowflakeMissingMachineId) == 0x101);
static_assert(Raw(ErrorCode::kSnowflakeInvalidMachineId) == 0x102);

struct NamedCode {
  ErrorCode code;
  const char* name;
};

// Every public bedrock::ErrorCode enumerator, for readable diagnostics.
constexpr NamedCode kNamedCodes[] = {
    {ErrorCode::kSuccess, "kSuccess"},
    {ErrorCode::kNoENT, "kNoENT"},
    {ErrorCode::kNullInput, "kNullInput"},
    {ErrorCode::kCorrupted, "kCorrupted"},
    {ErrorCode::kNotReady, "kNotReady"},
    {ErrorCode::kAlreadyConsumed, "kAlreadyConsumed"},
    {ErrorCode::kSnowflakeMissingMachineId, "kSnowflakeMissingMachineId"},
    {ErrorCode::kSnowflakeInvalidMachineId, "kSnowflakeInvalidMachineId"},
    {ErrorCode::kSnowflakeInvalidStatePath, "kSnowflakeInvalidStatePath"},
    {ErrorCode::kSnowflakeEnvironmentReadFailed, "kSnowflakeEnvironmentReadFailed"},
    {ErrorCode::kSnowflakeConfigurationReadFailed, "kSnowflakeConfigurationReadFailed"},
    {ErrorCode::kSnowflakeRegistryIoFailed, "kSnowflakeRegistryIoFailed"},
    {ErrorCode::kSnowflakeRegistryCorrupt, "kSnowflakeRegistryCorrupt"},
    {ErrorCode::kSnowflakeRegistryIncompatible, "kSnowflakeRegistryIncompatible"},
    {ErrorCode::kSnowflakeMachineIdMismatch, "kSnowflakeMachineIdMismatch"},
    {ErrorCode::kSnowflakeLockFailed, "kSnowflakeLockFailed"},
    {ErrorCode::kSnowflakeSlotsExhausted, "kSnowflakeSlotsExhausted"},
    {ErrorCode::kSnowflakeClockBeforeEpoch, "kSnowflakeClockBeforeEpoch"},
    {ErrorCode::kSnowflakeClockOutOfRange, "kSnowflakeClockOutOfRange"},
    {ErrorCode::kSnowflakeClockRollback, "kSnowflakeClockRollback"},
    {ErrorCode::kSnowflakeSequenceExhausted, "kSnowflakeSequenceExhausted"},
    {ErrorCode::kSnowflakeUnsupportedPlatform, "kSnowflakeUnsupportedPlatform"},
    {ErrorCode::kError, "kError"},
};

std::string CodeText(std::uint64_t code) {
  const char* name = "(not a bedrock::ErrorCode)";
  for (const NamedCode& named : kNamedCodes) {
    if (Raw(named.code) == code) name = named.name;
  }
  char hex[32];
  std::snprintf(hex, sizeof(hex), "0x%llX ", static_cast<unsigned long long>(code));
  return std::string(hex) + name;
}

ChildEnvironmentValue Absent() { return ChildEnvironmentValue::Absent(); }
ChildEnvironmentValue Present(std::string value) {
  return ChildEnvironmentValue::Present(std::move(value));
}

AssertionResult MakeDirectory(const fs::path& directory) {
  std::error_code ec;
  if (!fs::create_directory(directory, ec) || ec)
    return AssertionFailure() << "cannot create " << bedrock::test::PathToUtf8(directory)
                              << ": " << ec.message();
  return AssertionSuccess();
}

// One isolated test case: work/ is every child's working directory and
// reports/ receives the child reports. Each child gets its own fresh state
// directory, so no child observes a registry or cache left by another one.
// RunSnowflakeHelper reaps every child before it returns, so the directory is
// never removed under a running child.
class ArchiveCase final {
 public:
  AssertionResult Create(std::string_view label) {
    std::string diagnostic;
    if (!root_.Create(label, diagnostic)) return AssertionFailure() << diagnostic;
    work_ = root_.Path() / "work";
    reports_ = root_.Path() / "reports";
    if (auto made = MakeDirectory(work_); !made) return made;
    return MakeDirectory(reports_);
  }
  // Creates the existing absolute directory <root>/state-<name>.
  AssertionResult NewState(std::string_view name, fs::path& state) const {
    state = root_.Path() / bedrock::test::PathFromUtf8("state-" + std::string(name));
    return MakeDirectory(state);
  }
  const fs::path& Work() const noexcept { return work_; }
  fs::path Report(std::string_view case_id) const {
    return reports_ / bedrock::test::PathFromUtf8(std::string(case_id) + ".report");
  }

 private:
  bedrock::test::ScopedTestDirectory root_;
  fs::path work_;
  fs::path reports_;
};

AssertionResult FileIs(const fs::path& path, bool expected_present) {
  bool exists = !expected_present;
  std::string diagnostic;
  if (!bedrock::test::FileExists(path, exists, diagnostic)) return AssertionFailure() << diagnostic;
  if (exists != expected_present)
    return AssertionFailure() << (expected_present ? "missing " : "unexpected ")
                              << bedrock::test::PathToUtf8(path);
  return AssertionSuccess();
}
AssertionResult RegistryAbsent(const fs::path& state) {
  return FileIs(state / "snowflake.registry", false);
}
AssertionResult RegistryPresent(const fs::path& state) {
  return FileIs(state / "snowflake.registry", true);
}
AssertionResult MachineFileAbsent(const fs::path& state) {
  return FileIs(state / "machine-id", false);
}

AssertionResult WriteMachineFile(const fs::path& state, std::string_view bytes) {
  std::string diagnostic, back;
  const fs::path path = state / "machine-id";
  if (!bedrock::test::WriteFileBytes(path, bytes, diagnostic) ||
      !bedrock::test::ReadFileBytes(path, back, diagnostic))
    return AssertionFailure() << diagnostic;
  if (back != bytes) return AssertionFailure() << "machine-id bytes not preserved";
  return AssertionSuccess();
}
AssertionResult MachineFileIs(const fs::path& state, std::string_view bytes) {
  std::string diagnostic, back;
  if (!bedrock::test::ReadFileBytes(state / "machine-id", back, diagnostic))
    return AssertionFailure() << diagnostic;
  if (back != bytes) return AssertionFailure() << "machine-id changed to '" << back << "'";
  return AssertionSuccess();
}

// Runs `probe-archive <submode>` once in a fresh child whose state directory
// variable names `state` and whose machine variable is `machine`.
AssertionResult RunArchiveProbe(const ArchiveCase& layout, std::string_view case_id,
                                std::string_view submode, const fs::path& state,
                                ChildEnvironmentValue machine,
                                std::initializer_list<std::string_view> keys,
                                ChildReport& report) {
  const fs::path report_path = layout.Report(case_id);
  SnowflakeChildSpec spec;
  std::string diagnostic;
  if (!bedrock::test::MakeHelperSpec("probe-archive", report_path, case_id,
                                     {std::string(submode)}, layout.Work(),
                                     Present(bedrock::test::PathToUtf8(state)),
                                     std::move(machine), spec, diagnostic))
    return AssertionFailure() << diagnostic;
  if (!bedrock::test::RunSnowflakeHelper(spec, report_path, case_id, keys, report, diagnostic))
    return AssertionFailure() << std::string(case_id) << ": " << diagnostic;
  const std::string name(case_id);
  for (const auto& entry : report.Entries())
    std::printf("[sf-archive] %s %s=%s\n", name.c_str(), entry.first.c_str(),
                entry.second.c_str());
  std::fflush(stdout);
  return AssertionSuccess();
}

AssertionResult ReadUInt(const ChildReport& report, const char* key, std::uint64_t& value) {
  std::string diagnostic;
  if (!report.UInt64(key, value, diagnostic)) return AssertionFailure() << diagnostic;
  return AssertionSuccess();
}

// Non-fatal on purpose: callers check several independent report fields.
void ExpectCode(const ChildReport& report, const char* key, ErrorCode expected) {
  std::uint64_t value = 0;
  if (auto read = ReadUInt(report, key, value); !read) {
    ADD_FAILURE() << read.message();
    return;
  }
  EXPECT_EQ(value, Raw(expected)) << key << " is " << CodeText(value) << ", expected "
                                  << CodeText(Raw(expected));
}
void ExpectUInt(const ChildReport& report, const char* key, std::uint64_t expected) {
  std::uint64_t value = 0;
  if (auto read = ReadUInt(report, key, value); !read) {
    ADD_FAILURE() << read.message();
    return;
  }
  EXPECT_EQ(value, expected) << key;
}
void ExpectInt(const ChildReport& report, const char* key, std::int64_t expected) {
  std::int64_t value = 0;
  std::string diagnostic;
  if (!report.Int64(key, value, diagnostic)) {
    ADD_FAILURE() << diagnostic;
    return;
  }
  EXPECT_EQ(value, expected) << key;
}
// The reported status must not be a generic or wrong Snowflake code.
void ExpectNotCode(const ChildReport& report, const char* key, ErrorCode unwanted) {
  std::uint64_t value = 0;
  if (auto read = ReadUInt(report, key, value); !read) {
    ADD_FAILURE() << read.message();
    return;
  }
  EXPECT_NE(value, Raw(unwanted)) << key << " is " << CodeText(value);
}

constexpr ErrorCode kMissing = ErrorCode::kSnowflakeMissingMachineId;
constexpr ErrorCode kInvalid = ErrorCode::kSnowflakeInvalidMachineId;
constexpr std::int64_t kUntouchedValue = 99;  // probe destination before Load

// ---------------------------------------------------------------------------
// SnowflakeApiProcess: YAML Archive statuses across the public API boundary.
// ---------------------------------------------------------------------------
TEST(SnowflakeApiProcess, MissingMachineDumpReturnsExactStatusAndEmptyOutput) {
  ArchiveCase layout;
  ASSERT_TRUE(layout.Create("api-missing-dump"));
  fs::path state;
  ASSERT_TRUE(layout.NewState("dump", state));
  ASSERT_TRUE(MachineFileAbsent(state));
  ChildReport report;
  ASSERT_TRUE(RunArchiveProbe(layout, "missing-dump", "dump", state, Absent(),
                              {"dump_code", "dump_get_status", "dump_failed", "output_size"},
                              report));
  ExpectCode(report, "dump_code", kMissing);
  ExpectCode(report, "dump_get_status", kMissing);
  ExpectUInt(report, "dump_failed", 1);
  ExpectUInt(report, "output_size", 0);
  EXPECT_TRUE(RegistryAbsent(state));
}

TEST(SnowflakeApiProcess, MissingMachineInitializeReturnsExactStatus) {
  ArchiveCase layout;
  ASSERT_TRUE(layout.Create("api-missing-initialize"));
  fs::path state;
  ASSERT_TRUE(layout.NewState("initialize", state));
  ASSERT_TRUE(MachineFileAbsent(state));
  ChildReport report;
  ASSERT_TRUE(RunArchiveProbe(layout, "missing-initialize", "initialize", state, Absent(),
                              {"initialize_code", "initialize_get_status", "initialize_failed"},
                              report));
  ExpectCode(report, "initialize_code", kMissing);
  ExpectCode(report, "initialize_get_status", kMissing);
  ExpectUInt(report, "initialize_failed", 1);
  for (const char* key : {"initialize_code", "initialize_get_status"}) {
    for (const ErrorCode generic : {ErrorCode::kCorrupted, ErrorCode::kError, ErrorCode::kNotReady})
      ExpectNotCode(report, key, generic);
  }
  EXPECT_TRUE(RegistryAbsent(state));
}

TEST(SnowflakeApiProcess, FailedInitializeDoesNotConstructDestination) {
  ArchiveCase layout;
  ASSERT_TRUE(layout.Create("api-missing-load"));
  fs::path state;
  ASSERT_TRUE(layout.NewState("load", state));
  ASSERT_TRUE(MachineFileAbsent(state));
  ChildReport report;
  ASSERT_TRUE(RunArchiveProbe(layout, "missing-load", "load-after-failure", state, Absent(),
                              {"initialize_code", "initialize_get_status", "load_code",
                               "load_get_status", "destination_value", "accept_calls"},
                              report));
  std::uint64_t initialized = 0;
  ASSERT_TRUE(ReadUInt(report, "initialize_code", initialized));
  ASSERT_NE(initialized, Raw(ErrorCode::kSuccess)) << "Initialize must fail in this child";
  ExpectCode(report, "initialize_code", kMissing);
  ExpectCode(report, "initialize_get_status", kMissing);
  ExpectCode(report, "load_code", kMissing);
  ExpectCode(report, "load_get_status", kMissing);
  ExpectInt(report, "destination_value", kUntouchedValue);
  ExpectUInt(report, "accept_calls", 0);
  EXPECT_TRUE(RegistryAbsent(state));
}

TEST(SnowflakeApiProcess, InvalidMachineIsNotReportedAsMissing) {
  ArchiveCase layout;
  ASSERT_TRUE(layout.Create("api-invalid"));
  // A present invalid environment value wins and never falls back to the
  // valid machine-id file, so the status must be 0x102, not 0x101.
  constexpr std::string_view kValidFile = "3";
  {
    SCOPED_TRACE("dump");
    fs::path state;
    ASSERT_TRUE(layout.NewState("dump", state));
    ASSERT_TRUE(WriteMachineFile(state, kValidFile));
    ChildReport report;
    ASSERT_TRUE(RunArchiveProbe(layout, "invalid-dump", "dump", state, Present("8"),
                                {"dump_code", "dump_get_status", "dump_failed", "output_size"},
                                report));
    ExpectCode(report, "dump_code", kInvalid);
    ExpectCode(report, "dump_get_status", kInvalid);
    ExpectNotCode(report, "dump_code", kMissing);
    ExpectNotCode(report, "dump_get_status", kMissing);
    ExpectUInt(report, "dump_failed", 1);
    ExpectUInt(report, "output_size", 0);
    EXPECT_TRUE(RegistryAbsent(state));
    EXPECT_TRUE(MachineFileIs(state, kValidFile));
  }
  {
    SCOPED_TRACE("initialize");
    fs::path state;
    ASSERT_TRUE(layout.NewState("initialize", state));
    ASSERT_TRUE(WriteMachineFile(state, kValidFile));
    ChildReport report;
    ASSERT_TRUE(RunArchiveProbe(layout, "invalid-initialize", "initialize", state, Present("8"),
                                {"initialize_code", "initialize_get_status", "initialize_failed"},
                                report));
    ExpectCode(report, "initialize_code", kInvalid);
    ExpectCode(report, "initialize_get_status", kInvalid);
    ExpectNotCode(report, "initialize_code", kMissing);
    ExpectNotCode(report, "initialize_get_status", kMissing);
    ExpectUInt(report, "initialize_failed", 1);
    EXPECT_TRUE(RegistryAbsent(state));
    EXPECT_TRUE(MachineFileIs(state, kValidFile));
  }
  {
    SCOPED_TRACE("load-after-failure");
    fs::path state;
    ASSERT_TRUE(layout.NewState("load", state));
    ASSERT_TRUE(WriteMachineFile(state, kValidFile));
    ChildReport report;
    ASSERT_TRUE(RunArchiveProbe(layout, "invalid-load", "load-after-failure", state, Present("8"),
                                {"initialize_code", "initialize_get_status", "load_code",
                                 "load_get_status", "destination_value", "accept_calls"},
                                report));
    ExpectCode(report, "initialize_code", kInvalid);
    ExpectCode(report, "initialize_get_status", kInvalid);
    ExpectCode(report, "load_code", kInvalid);
    ExpectCode(report, "load_get_status", kInvalid);
    ExpectInt(report, "destination_value", kUntouchedValue);
    ExpectUInt(report, "accept_calls", 0);
    EXPECT_TRUE(RegistryAbsent(state));
    EXPECT_TRUE(MachineFileIs(state, kValidFile));
  }
}

TEST(SnowflakeApiProcess, ValidConfigurationProducesOutput) {
  ArchiveCase layout;
  ASSERT_TRUE(layout.Create("api-valid"));
  {
    SCOPED_TRACE("dump");
    fs::path state;
    ASSERT_TRUE(layout.NewState("dump", state));
    ASSERT_TRUE(MachineFileAbsent(state));
    ChildReport report;
    ASSERT_TRUE(RunArchiveProbe(layout, "valid-dump", "dump", state, Present("3"),
                                {"dump_code", "dump_get_status", "dump_failed", "output_size"},
                                report));
    ExpectCode(report, "dump_code", ErrorCode::kSuccess);
    ExpectCode(report, "dump_get_status", ErrorCode::kSuccess);
    ExpectUInt(report, "dump_failed", 0);
    std::uint64_t size = 0;
    ASSERT_TRUE(ReadUInt(report, "output_size", size));
    EXPECT_GT(size, 0u);
    // Positive control for the failure cases: a successful issuer creates
    // the registry in the same kind of fresh state directory.
    EXPECT_TRUE(RegistryPresent(state));
  }
  {
    SCOPED_TRACE("load");
    fs::path state;
    ASSERT_TRUE(layout.NewState("load", state));
    ASSERT_TRUE(MachineFileAbsent(state));
    ChildReport report;
    ASSERT_TRUE(RunArchiveProbe(layout, "valid-load", "load-after-failure", state, Present("3"),
                                {"initialize_code", "initialize_get_status", "load_code",
                                 "load_get_status", "destination_value", "accept_calls"},
                                report));
    ExpectCode(report, "initialize_code", ErrorCode::kSuccess);
    ExpectCode(report, "initialize_get_status", ErrorCode::kSuccess);
    ExpectCode(report, "load_code", ErrorCode::kSuccess);
    ExpectCode(report, "load_get_status", ErrorCode::kSuccess);
    ExpectInt(report, "destination_value", 1);
    ExpectUInt(report, "accept_calls", 1);
    EXPECT_TRUE(RegistryPresent(state));
  }
}

}  // namespace
