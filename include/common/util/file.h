#pragma once
#include <filesystem>
#include <vector>
#include <span>

namespace bedrock::util {

std::string ReadEntireFileIntoString(std::filesystem::path path);
void WriteToFile(std::filesystem::path path, std::vector<std::uint8_t> data);
void WriteToFile(std::filesystem::path path, std::string data);

std::uint64_t FindFirstApperenceFromFile(std::filesystem::path path,
                                               std::string_view pattern);
std::uint64_t FindFirstApperenceFromFile(
    std::filesystem::path path, std::span<const std::uint8_t> pattern);

}  // namespace bedrock::util
