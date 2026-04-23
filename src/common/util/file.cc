#include "common/util/file.h"

#include <fstream>

#include "common/util/memory.h"

namespace bedrock::util {

std::string ReadEntireFileIntoString(std::filesystem::path path) {
  std::ifstream file(path);
  if (!file) {
    return {};
  }

  file.seekg(0, std::ios::end);
  // 현재 위치 = 파일 크기
  std::size_t size = static_cast<std::size_t>(file.tellg());
  file.seekg(0, std::ios::beg);

  std::string contents;
  contents.resize(size);
  file.read(contents.data(), static_cast<std::streamsize>(size));

  return contents;
}

void WriteToFile(std::filesystem::path path, std::vector<std::uint8_t> data) {
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    return;
  }

  file.write(reinterpret_cast<const char*>(data.data()),
             static_cast<std::streamsize>(data.size()));

  return;
}

void WriteToFile(std::filesystem::path path, std::string data) {
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    return;
  }

  file.write(data.data(), static_cast<std::streamsize>(data.size()));

  return;
}

std::uint64_t FindFirstApperenceFromFile(std::filesystem::path path,
                                  std::string_view pattern) {
  std::ifstream file(path, std::ios::binary);
  std::uint64_t offset = 0;
  std::size_t carry = 0;
  std::size_t data_total_size = 0;

  std::size_t pattern_size = pattern.size();

  std::size_t chunk_size = 4 * 1024 * 1024;  // 4Mib
  std::string chunk = "";
  chunk.resize(chunk_size + pattern_size);

  if (chunk_size < pattern_size) {
    return -1;
  }

  while (true) {
    file.read(reinterpret_cast<char *>(chunk.data() + carry), chunk_size);
    std::size_t read_bytes = file.gcount();
    
    if (read_bytes == 0) {
      break;
    }
    
    data_total_size = carry + read_bytes;
    std::string_view chunk_span = chunk;
    chunk_span = chunk_span.substr(0, data_total_size);

    auto it = FindPatternFromData(chunk_span, pattern);

    if (it != chunk_span.end()) {
      return offset - carry + std::distance(chunk_span.begin(), it);
    }

    carry = std::min(pattern_size - 1, data_total_size);

    std::memmove(chunk.data(), chunk.data() + data_total_size - carry, carry);

    offset += read_bytes;
  }

  return offset;
}
std::uint64_t FindFirstApperenceFromFile(std::filesystem::path path,
                                  std::span<const std::uint8_t> pattern) {
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
    file.read(reinterpret_cast<char *>(chunk.data() + carry), chunk_size);
    std::size_t read_bytes = file.gcount();

    if (read_bytes == 0) {
      break;
    }

    data_total_size = carry + read_bytes;
    std::span<const std::uint8_t> chunk_span = chunk;
    chunk_span = chunk_span.subspan(0, data_total_size);

    auto it = FindPatternFromData(chunk_span, pattern);

    if (it != chunk_span.end()) {
      return offset - carry + std::distance(chunk_span.begin(), it);
    }

    carry = std::min(pattern_size - 1, data_total_size);

    std::memmove(chunk.data(), chunk.data() + data_total_size - carry, carry);

    offset += read_bytes;
  }

  return offset;
}

}  // namespace bedrock::util
