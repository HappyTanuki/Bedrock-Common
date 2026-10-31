#include "scoped_snowflake_config.h"

#include "snowflake_child.h"

#include <atomic>
#include <chrono>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace bedrock::test {

ScopedSnowflakeConfig::ScopedSnowflakeConfig(std::string machine_id) noexcept {
  // Wide/native environment APIs keep absent and present-empty distinct, and
  // they are the same block the library reads.
  if (!ReadProcessEnvironment(kStateDirectoryVariable, had_state_dir_, old_state_dir_) ||
      !ReadProcessEnvironment(kMachineIdVariable, had_machine_id_, old_machine_id_)) {
    error_ = "cannot read the current Snowflake environment";
    return;
  }
  std::error_code ec;
  const auto temp = std::filesystem::temp_directory_path(ec);
  if (ec) {
    error_ = "cannot determine temporary directory: " + ec.message();
    return;
  }
#ifdef _WIN32
  const auto process = static_cast<unsigned long long>(GetCurrentProcessId());
#else
  const auto process = static_cast<unsigned long long>(getpid());
#endif
  static std::atomic<unsigned> counter{0};
  const auto ticks = static_cast<unsigned long long>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  directory_ = temp / ("bedrock-snowflake-" + std::to_string(process) + "-" +
                       std::to_string(counter.fetch_add(1)) + "-" + std::to_string(ticks));
  directory_created_ = std::filesystem::create_directory(directory_, ec);
  if (ec || !directory_created_) {
    directory_created_ = false;
    error_ = "cannot create isolated Snowflake state directory";
    if (ec) error_ += ": " + ec.message();
    return;
  }
  std::string diagnostic;
  if (!WriteFileBytes(directory_ / "machine-id", machine_id, diagnostic)) {
    error_ = "cannot write test machine-id: " + diagnostic;
    Rollback();
    return;
  }
  state_env_changed_ = WriteProcessEnvironment(
      kStateDirectoryVariable, ChildEnvironmentValue::Present(PathToUtf8(directory_)));
  if (!state_env_changed_) {
    error_ = "cannot set BEDROCK_COMMON_STATE_DIR";
    Rollback();
    return;
  }
  machine_env_changed_ =
      WriteProcessEnvironment(kMachineIdVariable, ChildEnvironmentValue::Present(machine_id));
  if (!machine_env_changed_) {
    error_ = "cannot set BEDROCK_SNOWFLAKE_MACHINE_ID";
    Rollback();
  }
}

void ScopedSnowflakeConfig::Rollback() noexcept {
  // Restore exactly: an absent variable is removed, a present-empty one stays
  // present with an empty value.
  if (machine_env_changed_)
    (void)WriteProcessEnvironment(kMachineIdVariable,
                                  ChildEnvironmentValue{had_machine_id_, old_machine_id_});
  if (state_env_changed_)
    (void)WriteProcessEnvironment(kStateDirectoryVariable,
                                  ChildEnvironmentValue{had_state_dir_, old_state_dir_});
  machine_env_changed_ = false;
  state_env_changed_ = false;
  if (directory_created_) {
    std::error_code ec;
    std::filesystem::remove_all(directory_, ec);
    directory_created_ = false;
  }
}

ScopedSnowflakeConfig::~ScopedSnowflakeConfig() { Rollback(); }

}  // namespace bedrock::test
