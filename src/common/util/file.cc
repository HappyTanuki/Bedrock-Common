#include "common/util/file.h"

#include <cstring>
#include <fstream>

#include "common/util/memory.h"

namespace bedrock::util {

bool ReadEntireFile(
    std::filesystem::path path, std::string& out) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }

  out = std::string(std::istreambuf_iterator<char>(file),
              std::istreambuf_iterator<char>());

  return true;
}
bool ReadEntireFile(std::filesystem::path path,
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
bool WriteToFile(std::filesystem::path path,
                                std::string_view data) {
  auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(data.data()),
                         data.size());
  return WriteToFile(path, bytes);
}
bool WriteToFile(std::filesystem::path path,
                                std::span<const std::uint8_t> data) {
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }

  file.write(reinterpret_cast<const char*>(data.data()),
             static_cast<std::streamsize>(data.size()));

  return true;
}

std::uint64_t FindFirstAppearanceFromFile(std::filesystem::path path,
                                         std::string_view pattern) {
  auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(pattern.data()),
                         pattern.size());

  return FindFirstAppearanceFromFile(path, bytes);
}
std::uint64_t FindFirstAppearanceFromFile(
    std::filesystem::path path, std::span<const std::uint8_t> pattern) {
  std::ifstream file(path, std::ios::binary);
  std::uint64_t offset = 0;
  std::size_t carry = 0;
  std::size_t data_total_size = 0;

  std::size_t pattern_size = pattern.size();

  std::size_t chunk_size = 4 * 1024 * 1024;  // 4Mib
  std::vector<std::uint8_t> chunk(chunk_size + pattern_size);

  if (chunk_size < pattern_size) {
    return -1;
  }

  while (true) {
    file.read(reinterpret_cast<char*>(chunk.data() + carry), chunk_size);
    std::size_t read_bytes = file.gcount();

    if (read_bytes == 0) {
      break;
    }

    data_total_size = carry + read_bytes;
    std::span<const std::uint8_t> chunk_span = chunk;
    chunk_span = chunk_span.subspan(0, data_total_size);

    auto it = FindPatternFromData(chunk_span, pattern);

    if (it != chunk_span.end()) {
      // Because the offset is the position of the pattern in the file, we need
      // to add the offset of the chunk and subtract the carry (the part of the
      // previous chunk that we added to the current chunk) to get the correct
      // position in the file.
      return offset - carry + std::distance(chunk_span.begin(), it);
    }

    carry = std::min(pattern_size - 1, data_total_size);

    std::memmove(chunk.data(), chunk.data() + data_total_size - carry, carry);

    offset += read_bytes;
  }

  return offset;
}

}  // namespace bedrock::util
