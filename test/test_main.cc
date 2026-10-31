#include "scoped_snowflake_config.h"

#include <gtest/gtest.h>
#include <iostream>
#include <thread>

int RunSnowflakeProcessHelper(int argc, char** argv);

int main(int argc, char** argv) {
  // Helper children are dispatched before GoogleTest and before the fixture
  // below, so they observe only the environment chosen by their parent.
  const int helper = RunSnowflakeProcessHelper(argc, argv);
  if (helper >= 0) return helper;
  ::testing::InitGoogleTest(&argc, argv);
  if (GTEST_FLAG_GET(list_tests)) return RUN_ALL_TESTS();  // Discovery only.
  bedrock::test::ScopedSnowflakeConfig config;
  if (!config.Ok()) {
    std::cerr << "Snowflake test setup failed: " << config.Error() << '\n';
    return 2;
  }
  int result = 1;
  std::thread runner([&] { result = RUN_ALL_TESTS(); });
  runner.join();  // Destroy runner-thread TLS before config removes its directory.
  return result;
}
