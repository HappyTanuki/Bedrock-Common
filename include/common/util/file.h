#pragma once
#include <filesystem>
#include <vector>
#include <span>

namespace bedrock::util {

bool ReadEntireFile(std::filesystem::path path, std::string& out);
bool ReadEntireFile(std::filesystem::path path, std::vector<std::uint8_t>& out);
bool WriteToFile(std::filesystem::path path,
                 std::span<const std::uint8_t> data);
bool WriteToFile(std::filesystem::path path, std::string_view data);

std::uint64_t FindFirstAppearanceFromFile(const std::filesystem::path& path,
                                          std::string_view pattern);
std::uint64_t FindFirstAppearanceFromFile(
    std::filesystem::path path, std::span<const std::uint8_t> pattern);

}  // namespace bedrock::util
