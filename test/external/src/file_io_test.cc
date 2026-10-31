/**
 * @file file_io_test.cc
 * @brief WriteToFile과 FileContents의 파일 읽기 테스트.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include "common/util/file.h"

namespace {

class FileIoTest : public ::testing::Test {
 protected:
  std::filesystem::path Path(const char* name) const {
    return std::filesystem::temp_directory_path() / name;
  }

  void TearDown() override {
    std::error_code error;
    std::filesystem::remove(path_, error);
  }

  std::filesystem::path path_;
};

TEST_F(FileIoTest, ReadsEmptyFile) {
  path_ = Path("bedrock_common_file_io_empty.bin");
  ASSERT_TRUE(bedrock::util::WriteToFile(path_, std::string_view{}));

  bedrock::util::FileContents contents;
  ASSERT_TRUE(contents.Read(path_).Ok());
  EXPECT_TRUE(contents.Bytes().empty());
  EXPECT_TRUE(contents.Text().empty());
}

TEST_F(FileIoTest, PreservesBinaryBytesInViews) {
  path_ = Path("bedrock_common_file_io_binary.bin");
  const std::array<std::uint8_t, 4> expected_bytes{0x00, 0x80, 0xff, 'x'};
  const std::string expected_text{static_cast<char>(0), static_cast<char>(0x80),
                                  static_cast<char>(0xff), 'x'};
  ASSERT_TRUE(bedrock::util::WriteToFile(
      path_, std::span<const std::uint8_t>(expected_bytes)));

  bedrock::util::FileContents contents;
  ASSERT_TRUE(contents.Read(path_).Ok());
  ASSERT_EQ(contents.Bytes().size(), expected_bytes.size());
  EXPECT_TRUE(std::equal(contents.Bytes().begin(), contents.Bytes().end(),
                         expected_bytes.begin()));
  EXPECT_EQ(contents.Text(), std::string_view(expected_text));
}

TEST_F(FileIoTest, ReadsFilesLargerThanOneChunkExactly) {
  path_ = Path("bedrock_common_file_io_large.bin");
  const std::string expected(128 * 1024 + 37, static_cast<char>(0xa5));
  ASSERT_TRUE(bedrock::util::WriteToFile(path_, expected));

  bedrock::util::FileContents contents;
  ASSERT_TRUE(contents.Read(path_).Ok());
  ASSERT_EQ(contents.Bytes().size(), expected.size());
  EXPECT_TRUE(
      std::equal(contents.Bytes().begin(), contents.Bytes().end(),
                 reinterpret_cast<const std::uint8_t*>(expected.data())));
  EXPECT_EQ(contents.Text(), std::string_view(expected));
}

TEST_F(FileIoTest, MissingFileReturnsNoEntryAndPreservesContents) {
  path_ = Path("bedrock_common_file_io_missing.bin");
  std::error_code error;
  std::filesystem::remove(path_, error);

  bedrock::util::FileContents contents;
  const auto fresh_status = contents.Read(path_);
  EXPECT_EQ(fresh_status.code, bedrock::ErrorCode::kNoENT);
  EXPECT_TRUE(contents.Bytes().empty());
  EXPECT_TRUE(contents.Text().empty());

  const std::string expected = "previous contents";
  ASSERT_TRUE(bedrock::util::WriteToFile(path_, expected));
  ASSERT_TRUE(contents.Read(path_).Ok());
  const auto previous_bytes = contents.Bytes();
  const auto previous_text = contents.Text();

  std::filesystem::remove(path_, error);
  const auto failed_status = contents.Read(path_);
  EXPECT_EQ(failed_status.code, bedrock::ErrorCode::kNoENT);
  EXPECT_EQ(contents.Bytes().data(), previous_bytes.data());
  EXPECT_EQ(contents.Bytes().size(), previous_bytes.size());
  EXPECT_EQ(contents.Text(), previous_text);
}

TEST_F(FileIoTest, MoveConstructionPreservesStorageAndMovedFromCanReadAgain) {
  path_ = Path("bedrock_common_file_io_move_construct.bin");
  const std::string expected = "move constructor contents";
  ASSERT_TRUE(bedrock::util::WriteToFile(path_, expected));

  bedrock::util::FileContents source;
  ASSERT_TRUE(source.Read(path_).Ok());
  const auto* const original_data = source.Bytes().data();
  bedrock::util::FileContents moved(std::move(source));

  EXPECT_EQ(moved.Bytes().data(), original_data);
  EXPECT_EQ(moved.Text(), std::string_view(expected));
  EXPECT_TRUE(source.Bytes().empty());
  EXPECT_TRUE(source.Text().empty());
  ASSERT_TRUE(source.Read(path_).Ok());
  EXPECT_EQ(source.Text(), std::string_view(expected));
}

TEST_F(FileIoTest, MoveAssignmentPreservesStorageAndMovedFromCanReadAgain) {
  path_ = Path("bedrock_common_file_io_move_assign.bin");
  const std::string expected = "move assignment contents";
  ASSERT_TRUE(bedrock::util::WriteToFile(path_, expected));

  bedrock::util::FileContents source;
  ASSERT_TRUE(source.Read(path_).Ok());
  bedrock::util::FileContents target;
  ASSERT_TRUE(target.Read(path_).Ok());
  const auto* const original_data = source.Bytes().data();

  target = std::move(source);
  EXPECT_EQ(target.Bytes().data(), original_data);
  EXPECT_EQ(target.Text(), std::string_view(expected));
  EXPECT_TRUE(source.Bytes().empty());
  EXPECT_TRUE(source.Text().empty());

  const std::string reread = "moved-from object reused";
  ASSERT_TRUE(bedrock::util::WriteToFile(path_, reread));
  ASSERT_TRUE(source.Read(path_).Ok());
  EXPECT_EQ(source.Text(), std::string_view(reread));
  EXPECT_EQ(target.Text(), std::string_view(expected));
}

}  // namespace
