#pragma once
#include <cstdint>
#include <span>
#include <string_view>

namespace bedrock::util {

std::string_view::iterator FindPatternFromData(std::string_view data,
                                               std::string_view pattern);
std::span<const std::uint8_t>::iterator FindPatternFromData(
    std::span<const std::uint8_t> data, std::span<const std::uint8_t> pattern);

}  // namespace bedrock::util