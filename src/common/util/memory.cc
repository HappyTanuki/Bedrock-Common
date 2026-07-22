/**
 * @file memory.cc
 * @brief 메모리(버퍼) 상에서 패턴을 검색하는 유틸리티 구현.
 */
#include "common/util/memory.h"
#include <algorithm>

namespace bedrock::util {

/**
 * @brief 컨테이너 안에서 패턴이 처음 나타나는 위치를 찾는 실제 구현.
 * @note 현재는 std::search를 사용하는 자리표시자(placeholder) 구현이며,
 *       추후 더 효율적인 패턴 검색 알고리즘으로 교체될 수 있다.
 * @tparam Container begin()/end()를 지원하는 컨테이너 타입.
 * @param data 검색 대상 컨테이너.
 * @param pattern 찾을 패턴.
 * @return 패턴을 찾은 위치의 반복자. 없으면 data.end().
 */
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
