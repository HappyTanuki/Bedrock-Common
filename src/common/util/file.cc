/**
 * @file file.cc
 * @brief 파일 읽기/쓰기 및 파일 내 패턴 검색 유틸리티 구현.
 */
#include "common/util/file.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

#include "common/util/memory.h"

namespace bedrock::util {
namespace {

constexpr std::size_t kFileReadChunkSize = 64 * 1024;

bool ReadFileBytes(std::ifstream& file, std::vector<std::uint8_t>& result) {
  std::array<std::uint8_t, kFileReadChunkSize> buffer{};
  while (true) {
    file.read(reinterpret_cast<char*>(buffer.data()),
              static_cast<std::streamsize>(buffer.size()));
    const std::streamsize count = file.gcount();
    if (count > 0) {
      result.insert(result.end(), buffer.begin(),
                    buffer.begin() + static_cast<std::size_t>(count));
    }
    if (file.eof()) {
      return !file.bad();
    }
    if (!file || count == 0) {
      return false;
    }
  }
}

}  // namespace

struct FileContents::Impl {
  std::vector<std::uint8_t> bytes;
};

FileContents::FileContents() : impl_(std::make_unique<Impl>()) {}
FileContents::~FileContents() = default;
FileContents::FileContents(FileContents&&) noexcept = default;
FileContents& FileContents::operator=(FileContents&&) noexcept = default;

bedrock::Status FileContents::Read(const std::filesystem::path& path) & {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return bedrock::Status(bedrock::ErrorCode::kNoENT);
  }

  std::vector<std::uint8_t> result;
  std::error_code size_error;
  const auto file_size = std::filesystem::file_size(path, size_error);
  if (!size_error) {
    const auto reserve_size =
        file_size > result.max_size()
            ? result.max_size()
            : static_cast<std::vector<std::uint8_t>::size_type>(file_size);
    result.reserve(reserve_size);
  }

  if (!ReadFileBytes(file, result)) {
    return bedrock::Status(bedrock::ErrorCode::kError);
  }

  if (!impl_) {
    impl_ = std::make_unique<Impl>();
  }
  impl_->bytes = std::move(result);
  return bedrock::Status(bedrock::ErrorCode::kSuccess);
}

std::span<const std::uint8_t> FileContents::Bytes() const noexcept {
  if (!impl_) {
    return {};
  }
  return impl_->bytes;
}

std::string_view FileContents::Text() const noexcept {
  if (!impl_ || impl_->bytes.empty()) {
    return {};
  }
  return {reinterpret_cast<const char*>(impl_->bytes.data()),
          impl_->bytes.size()};
}

bool WriteToFile(const std::filesystem::path& path, std::string_view data) {
  auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(data.data()),
                         data.size());
  return WriteToFile(path, bytes);
}
bool WriteToFile(const std::filesystem::path& path,
                 std::span<const std::uint8_t> data) {
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }

  file.write(reinterpret_cast<const char*>(data.data()),
             static_cast<std::streamsize>(data.size()));

  return true;
}

std::uint64_t FindFirstAppearanceFromFile(const std::filesystem::path& path,
                                          std::string_view pattern) {
  auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(pattern.data()),
                         pattern.size());

  return FindFirstAppearanceFromFile(path, bytes);
}
std::uint64_t FindFirstAppearanceFromFile(
    const std::filesystem::path& path, std::span<const std::uint8_t> pattern) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return kFileSearchFailed;  // 열기 실패와 "0번에서 발견"을 구분한다.
  }
  std::uint64_t offset = 0;
  std::size_t carry = 0;
  std::size_t data_total_size = 0;

  std::size_t pattern_size = pattern.size();

  auto chunk_size = static_cast<std::size_t>(4 * 1024 * 1024);  // 4Mib
  std::vector<std::uint8_t> chunk(chunk_size + pattern_size);

  if (chunk_size < pattern_size) {
    return kFileSearchFailed;
  }

  while (true) {
    file.read(reinterpret_cast<char*>(chunk.data() + carry),
              static_cast<std::streamsize>(chunk_size));
    const auto read_bytes = static_cast<std::size_t>(file.gcount());

    if (read_bytes == 0) {
      break;
    }

    data_total_size = carry + read_bytes;
    std::span<const std::uint8_t> chunk_span = chunk;
    chunk_span = chunk_span.subspan(0, data_total_size);

    const auto match_position = FindPatternFromData(chunk_span, pattern);

    if (match_position != chunk_span.end()) {
      // Because the offset is the position of the pattern in the file, we need
      // to add the offset of the chunk and subtract the carry (the part of the
      // previous chunk that we added to the current chunk) to get the correct
      // position in the file.
      return offset - carry +
             static_cast<std::uint64_t>(
                 std::distance(chunk_span.begin(), match_position));
    }

    carry = std::min(pattern_size - 1, data_total_size);

    std::memmove(chunk.data(), chunk.data() + data_total_size - carry, carry);

    offset += read_bytes;
  }

  return offset;
}

}  // namespace bedrock::util
