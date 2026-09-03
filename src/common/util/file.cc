/**
 * @file file.cc
 * @brief 파일 읽기/쓰기 및 파일 내 패턴 검색 유틸리티 구현.
 */
#include "common/util/file.h"

#include <cstddef>
#include <cstring>
#include <fstream>
#include <utility>

#include "common/util/memory.h"

namespace bedrock::util {

bool ReadEntireFile(const std::filesystem::path& path, std::string& out) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }

  out = std::string(std::istreambuf_iterator<char>(file),
                    std::istreambuf_iterator<char>());

  return true;
}
bool ReadEntireFile(const std::filesystem::path& path,
                    std::vector<std::uint8_t>& out) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }

  out = std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file),
                                  std::istreambuf_iterator<char>());

  return true;
}

// wrapper for string_view
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
