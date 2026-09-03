/**
 * @file file_io_test.cc
 * @brief WriteToFile과 ReadEntireFile의 문자열 왕복 테스트.
 */
#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "common/util/file.h"

TEST(FileIo, StringRoundTripsThroughFile) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     "bedrock_common_file_io_test.txt";
  const std::string expected = "test";
  std::string actual;

  ASSERT_TRUE(bedrock::util::WriteToFile(path, expected));
  ASSERT_TRUE(bedrock::util::ReadEntireFile(path, actual));
  EXPECT_EQ(actual, expected);

  std::error_code error;
  std::filesystem::remove(path, error);
}
