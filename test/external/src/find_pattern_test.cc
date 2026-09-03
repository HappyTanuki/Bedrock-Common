/**
 * @file find_pattern_test.cc
 * @brief 문자열 및 바이트 패턴 검색 오버로드의 결과 일치 테스트.
 */
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

#include "common/util/file.h"

TEST(FilePatternSearch, StringAndByteOverloadsFindSameFirstOffset) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     "bedrock_common_find_pattern_test.bin";
  ASSERT_TRUE(bedrock::util::WriteToFile(path, "prefix-text-suffix"));

  const std::uint64_t string_offset =
      bedrock::util::FindFirstAppearanceFromFile(path,
                                                 std::string_view("text"));
  const std::vector<std::uint8_t> pattern = {'t', 'e', 'x', 't'};
  const std::uint64_t byte_offset =
      bedrock::util::FindFirstAppearanceFromFile(path, pattern);

  EXPECT_EQ(string_offset, 7U);
  EXPECT_EQ(byte_offset, string_offset);
  std::error_code error;
  std::filesystem::remove(path, error);
}
