#include "common/util/file.h"

#include <fstream>

#include "common/util/memory.h"

namespace bedrock::util::file {

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

// Returns offset
template <typename Container>
std::size_t FindPatternFromFile(std::filesystem::path path, Container pattern) {
  std::ifstream file(path, std::ios::binary);
  std::size_t offset = 0;

  std::size_t chunk_size = 4 * 1024;  // 4Kib
  std::vector<std::uint8_t> chunk(chunk_size);

  if (!file) {
    // Which is max of std::size_t
    return -1;
  }

  // file.read(chunk.data(), chunk_size);

  // while (!file.eof()) {
  //   memory::FindPatternFromData(pattern, Container data);
  // }
}

}  // namespace bedrock::util::file
