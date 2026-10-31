// Child-side Snowflake helper modes. test_main.cc calls RunSnowflakeProcessHelper
// before GoogleTest initialization and before ScopedSnowflakeConfig, so each
// helper process sees only the environment its parent specified. The modes,
// arguments and report keys are documented in snowflake_child.h.
#include "snowflake_child.h"
#include "scoped_snowflake_config.h"

#include "common/archive.h"
#include "common/archive/transcriber/yaml_deserializer.h"
#include "common/archive/transcriber/yaml_serializer.h"
#include "common/data_types/snowflake.h"
#include "common/data_types/status.h"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using bedrock::test::ChildEnvironmentValue;
using bedrock::test::ChildReportWriter;

// Nonzero exits are harness failures only; library results go to reports.
constexpr int kExitUsage = 90;
constexpr int kExitArguments = 91;
constexpr int kExitReport = 92;
constexpr int kExitBarrierTimeout = 93;
constexpr int kExitEnvironment = 94;
constexpr int kExitIdsFile = 96;

constexpr std::chrono::seconds kBarrierTimeout{120};

struct HelperContext {
  std::vector<std::string> args;  // UTF-8; args[0] is the executable.
  std::filesystem::path report;
  std::string case_id;
};

int Fail(const HelperContext& context, int code, std::string_view message) {
  std::fprintf(stderr, "sf-helper failure %d: %.*s\n", code,
               static_cast<int>(message.size()), message.data());
  if (!context.report.empty()) {
    std::filesystem::path error_path = context.report;
    error_path += ".error";
    std::string ignored;
    (void)bedrock::test::WriteFileBytes(error_path, message, ignored);
  }
  return code;
}

int Publish(const HelperContext& context, const ChildReportWriter& writer,
            const std::filesystem::path& path) {
  std::string diagnostic;
  if (!writer.Commit(path, diagnostic)) return Fail(context, kExitReport, diagnostic);
  return 0;
}

bool WaitForFile(const std::filesystem::path& path) {
  const auto deadline = std::chrono::steady_clock::now() + kBarrierTimeout;
  while (std::chrono::steady_clock::now() < deadline) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && !ec) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return false;
}

bool ParseCount(std::string_view text, int& count) {
  const auto result = std::from_chars(text.data(), text.data() + text.size(), count);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size() &&
         count >= 1 && count <= 1000000;
}

int ProbeConfig(const HelperContext& context) {
  if (context.args.size() != 5) return Fail(context, kExitUsage, "probe-config <report> <case-id>");
  const std::int64_t result = bedrock::Snowflake::Generate();
  ChildReportWriter writer(context.case_id);
  writer.AddInt("result", result);
  return Publish(context, writer, context.report);
}

int ProbeCache(const HelperContext& context) {
  if (context.args.size() != 9)
    return Fail(context, kExitUsage,
                "probe-cache <first-report> <case-id> <continue-file> <final-report> "
                "<next-state-dir> <next-machine-id>");
  const auto continue_file = bedrock::test::PathFromUtf8(context.args[5]);
  const auto final_report = bedrock::test::PathFromUtf8(context.args[6]);
  const std::string& next_state = context.args[7];
  const std::string& next_machine = context.args[8];

  const std::int64_t first = bedrock::Snowflake::Generate();
  {
    ChildReportWriter writer(context.case_id);
    writer.AddInt("first_result", first);
    if (const int failed = Publish(context, writer, context.report); failed != 0)
      return failed;
  }
  if (!WaitForFile(continue_file))
    return Fail(context, kExitBarrierTimeout, "continue file never appeared");
  if (!bedrock::test::WriteProcessEnvironment(bedrock::test::kStateDirectoryVariable,
                                              ChildEnvironmentValue::Present(next_state)) ||
      !bedrock::test::WriteProcessEnvironment(bedrock::test::kMachineIdVariable,
                                              ChildEnvironmentValue::Present(next_machine)))
    return Fail(context, kExitEnvironment, "cannot change the child environment");
  const std::int64_t second = bedrock::Snowflake::Generate();
  std::int64_t thread_result = 0;
  bool thread_ran = false;
  std::thread issuer([&] {
    thread_result = bedrock::Snowflake::Generate();
    thread_ran = true;
  });
  issuer.join();  // Thread TLS (slot ownership) ends before reporting.
  if (!thread_ran) return Fail(context, kExitUsage, "new thread did not run");
  ChildReportWriter writer(context.case_id);
  writer.AddInt("first_result", first);
  writer.AddInt("second_result", second);
  writer.AddInt("thread_result", thread_result);
  return Publish(context, writer, final_report);
}

struct ProbeSchema final : bedrock::archive::Schema {
  std::int32_t value = 0;
  std::uint64_t accept_calls = 0;
  void Accept(bedrock::archive::Visitor& visitor) override {
    ++accept_calls;
    visitor.Visit("value", value);
  }
};

std::uint64_t Code(const bedrock::Status& status) {
  return static_cast<std::uint32_t>(status.code);
}

int ProbeArchive(const HelperContext& context) {
  if (context.args.size() != 6)
    return Fail(context, kExitUsage, "probe-archive <report> <case-id> <submode>");
  const std::string& submode = context.args[5];
  ChildReportWriter writer(context.case_id);
  if (submode == "dump") {
    ProbeSchema schema;
    schema.value = 1;
    bedrock::archive::transcriber::YAMLSerializer serializer;
    const bedrock::Status dumped = serializer.Dump(schema);
    const bedrock::Status current = serializer.GetStatus();
    const std::string_view output = serializer.Output();
    writer.AddUInt("dump_code", Code(dumped));
    writer.AddUInt("dump_get_status", Code(current));
    writer.AddUInt("dump_failed", dumped.Failed() ? 1u : 0u);
    writer.AddUInt("output_size", output.size());
    return Publish(context, writer, context.report);
  }
  if (submode == "initialize" || submode == "load-after-failure") {
    bedrock::archive::transcriber::YAMLDeserializer deserializer;
    const bedrock::Status initialized = deserializer.Initialize("value: 1\n");
    const bedrock::Status initialized_current = deserializer.GetStatus();
    writer.AddUInt("initialize_code", Code(initialized));
    writer.AddUInt("initialize_get_status", Code(initialized_current));
    if (submode == "initialize") {
      writer.AddUInt("initialize_failed", initialized.Failed() ? 1u : 0u);
      return Publish(context, writer, context.report);
    }
    ProbeSchema destination;
    destination.value = 99;
    const bedrock::Status loaded = deserializer.Load(destination);
    const bedrock::Status loaded_current = deserializer.GetStatus();
    writer.AddUInt("load_code", Code(loaded));
    writer.AddUInt("load_get_status", Code(loaded_current));
    writer.AddInt("destination_value", destination.value);
    writer.AddUInt("accept_calls", destination.accept_calls);
    return Publish(context, writer, context.report);
  }
  return Fail(context, kExitUsage, "unknown probe-archive submode");
}

int Generate(const HelperContext& context) {
  if (context.args.size() != 7)
    return Fail(context, kExitUsage, "generate <report> <case-id> <count> <ids-path>");
  int count = 0;
  if (!ParseCount(context.args[5], count)) return Fail(context, kExitUsage, "invalid count");
  const auto ids_path = bedrock::test::PathFromUtf8(context.args[6]);
  std::string ids;
  int generated = 0;
  std::int64_t last = 0;
  for (int i = 0; i < count; ++i) {
    last = bedrock::Snowflake::Generate();
    if (last < 0) break;  // No retry: the error is the reported result.
    ids += std::to_string(last);
    ids.push_back('\n');
    ++generated;
  }
  std::string diagnostic;
  if (!bedrock::test::WriteFileBytes(ids_path, ids, diagnostic))
    return Fail(context, kExitIdsFile, diagnostic);
  ChildReportWriter writer(context.case_id);
  writer.AddInt("requested", count);
  writer.AddInt("generated", generated);
  writer.AddInt("last_result", last);
  return Publish(context, writer, context.report);
}

int Hold(const HelperContext& context) {
  if (context.args.size() != 6)
    return Fail(context, kExitUsage, "hold <report> <case-id> <release-file>");
  const auto release = bedrock::test::PathFromUtf8(context.args[5]);
  const std::int64_t result = bedrock::Snowflake::Generate();
  ChildReportWriter writer(context.case_id);
  writer.AddInt("result", result);
  if (const int failed = Publish(context, writer, context.report); failed != 0) return failed;
  if (result < 0) return 0;  // No slot is held; the report carries the error.
  // Bounded: a failed parent must not leave an orphan owner forever.
  if (!WaitForFile(release)) return Fail(context, kExitBarrierTimeout, "release file never appeared");
  return 0;
}

int ProbeFixture(const HelperContext& context) {
  if (context.args.size() != 5) return Fail(context, kExitUsage, "probe-fixture <report> <case-id>");
  using bedrock::test::kMachineIdVariable;
  using bedrock::test::kStateDirectoryVariable;
  using bedrock::test::ReadProcessEnvironment;
  bool state_before = false, machine_before = false;
  std::string state_before_value, machine_before_value;
  if (!ReadProcessEnvironment(kStateDirectoryVariable, state_before, state_before_value) ||
      !ReadProcessEnvironment(kMachineIdVariable, machine_before, machine_before_value))
    return Fail(context, kExitEnvironment, "cannot read the initial environment");
  bool fixture_ok = false, state_matches = false, machine_is_3 = false, file_is_3 = false;
  std::filesystem::path directory;
  {
    bedrock::test::ScopedSnowflakeConfig config("3");
    fixture_ok = config.Ok();
    if (fixture_ok) {
      directory = config.StateDirectory();
      bool present = false;
      std::string value, diagnostic;
      state_matches = ReadProcessEnvironment(kStateDirectoryVariable, present, value) &&
                      present && value == bedrock::test::PathToUtf8(directory);
      machine_is_3 = ReadProcessEnvironment(kMachineIdVariable, present, value) &&
                     present && value == "3";
      std::string bytes;
      file_is_3 = bedrock::test::ReadFileBytes(directory / "machine-id", bytes, diagnostic) &&
                  bytes == "3";
    }
  }
  bool state_after = false, machine_after = false;
  std::string state_after_value, machine_after_value;
  if (!ReadProcessEnvironment(kStateDirectoryVariable, state_after, state_after_value) ||
      !ReadProcessEnvironment(kMachineIdVariable, machine_after, machine_after_value))
    return Fail(context, kExitEnvironment, "cannot read the restored environment");
  bool directory_exists = true;
  std::string diagnostic;
  if (!directory.empty() && !bedrock::test::FileExists(directory, directory_exists, diagnostic))
    return Fail(context, kExitEnvironment, diagnostic);
  if (state_after_value.find('\n') != std::string::npos ||
      machine_after_value.find('\n') != std::string::npos)
    return Fail(context, kExitEnvironment, "restored environment value contains a newline");
  ChildReportWriter writer(context.case_id);
  writer.AddUInt("state_present_before", state_before ? 1u : 0u);
  writer.AddUInt("machine_present_before", machine_before ? 1u : 0u);
  writer.AddUInt("fixture_ok", fixture_ok ? 1u : 0u);
  writer.AddUInt("state_matches_during", state_matches ? 1u : 0u);
  writer.AddUInt("machine_during_is_3", machine_is_3 ? 1u : 0u);
  writer.AddUInt("machine_file_is_3", file_is_3 ? 1u : 0u);
  writer.AddUInt("state_present_after", state_after ? 1u : 0u);
  writer.AddText("state_value_after", state_after_value);
  writer.AddUInt("machine_present_after", machine_after ? 1u : 0u);
  writer.AddText("machine_value_after", machine_after_value);
  writer.AddUInt("directory_removed", fixture_ok && !directory_exists ? 1u : 0u);
  return Publish(context, writer, context.report);
}

}  // namespace

// Returns -1 when argv is not a helper invocation; otherwise the exit code.
int RunSnowflakeProcessHelper(int argc, char** argv) {
  if (argc < 2 || argv[1] == nullptr ||
      std::string_view(argv[1]) != bedrock::test::kSnowflakeHelperFlag)
    return -1;
  HelperContext context;
  std::string diagnostic;
  if (!bedrock::test::NativeHelperArguments(argc, argv, context.args, diagnostic))
    return Fail(context, kExitArguments, diagnostic);
  if (context.args.size() < 5)
    return Fail(context, kExitUsage, "--sf-helper <mode> <report-path> <case-id> ...");
  for (const auto& argument : context.args) {
    if (!bedrock::test::IsValidUtf8(argument))
      return Fail(context, kExitArguments, "helper argument is not valid UTF-8");
  }
  context.report = bedrock::test::PathFromUtf8(context.args[3]);
  context.case_id = context.args[4];
  if (context.case_id.empty() || context.case_id.find_first_of("\r\n") != std::string::npos)
    return Fail(context, kExitArguments, "case id must be a non-empty single line");
  const std::string& mode = context.args[2];
  if (mode == "probe-config" || mode == "registry-seed") return ProbeConfig(context);
  if (mode == "probe-cache") return ProbeCache(context);
  if (mode == "probe-archive") return ProbeArchive(context);
  if (mode == "generate") return Generate(context);
  if (mode == "hold") return Hold(context);
  if (mode == "probe-fixture") return ProbeFixture(context);
  return Fail(context, kExitUsage, "unknown helper mode");
}
