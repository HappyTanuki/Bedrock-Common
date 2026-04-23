#include "common/util/memory.h"
#include <algorithm>

namespace bedrock::util {

template <typename Container>
Container::iterator FindPatternFromDataImpl(Container data, Container pattern) {
  // placeholder for more efficient pattern searching algorithm
  return std::search(data.begin(), data.end(), pattern.begin(), pattern.end());
}

std::string_view::iterator FindPatternFromData(std::string_view data,
                                               std::string_view pattern) {
  return FindPatternFromDataImpl<std::string_view>(data, pattern);
}
std::span<const std::uint8_t>::iterator FindPatternFromData(
    std::span<const std::uint8_t> data, std::span<const std::uint8_t> pattern) {
  return FindPatternFromDataImpl<std::span<const std::uint8_t>>(data, pattern);
}

}
