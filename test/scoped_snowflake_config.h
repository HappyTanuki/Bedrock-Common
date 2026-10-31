#pragma once

#include <filesystem>
#include <string>

namespace bedrock::test {
// Owns isolated state and configures the process before Snowflake calls. Must
// outlive and join every thread that can issue IDs. Reads and writes the
// native (wide on Windows) process environment that the library reads, and
// restores both variables exactly: absent stays absent, present-empty stays
// present with an empty value. Filesystem calls use error_code overloads.
class ScopedSnowflakeConfig final {
 public:
  explicit ScopedSnowflakeConfig(std::string machine_id = "0") noexcept;
  ~ScopedSnowflakeConfig();
  ScopedSnowflakeConfig(const ScopedSnowflakeConfig&) = delete;
  ScopedSnowflakeConfig& operator=(const ScopedSnowflakeConfig&) = delete;

  [[nodiscard]] bool Ok() const noexcept { return error_.empty(); }
  [[nodiscard]] const std::string& Error() const noexcept { return error_; }
  [[nodiscard]] const std::filesystem::path& StateDirectory() const noexcept {
    return directory_;
  }

 private:
  void Rollback() noexcept;
  std::filesystem::path directory_;
  std::string old_state_dir_;
  std::string old_machine_id_;
  std::string error_;
  bool had_state_dir_ = false;
  bool had_machine_id_ = false;
  bool state_env_changed_ = false;
  bool machine_env_changed_ = false;
  bool directory_created_ = false;
};
}  // namespace bedrock::test
