#pragma once
#include <filesystem>
#include <vector>

namespace bedrock::util::file {

std::string ReadEntireFileIntoString(std::filesystem::path path);
void WriteToFile(std::filesystem::path path, std::vector<std::uint8_t> data);
void WriteToFile(std::filesystem::path path, std::string data);

std::string_view::iterator FindPatternFromFile(std::filesystem::path path,
                                               std::string_view pattern);
std::span<const std::uint8_t>::iterator FindPatternFromFile(
    std::filesystem::path path, std::span<const std::uint8_t> pattern);

}  // namespace bedrock::util::file
