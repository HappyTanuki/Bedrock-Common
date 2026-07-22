/**
 * @file memory.h
 * @brief 메모리(버퍼) 상에서 패턴을 검색하는 유틸리티.
 */
#pragma once
#include <cstdint>
#include <span>
#include <string_view>

namespace bedrock::util {

/**
 * @brief 문자열에서 패턴이 처음 나타나는 위치를 찾는다.
 * @param data 검색 대상 문자열.
 * @param pattern 찾을 패턴.
 * @return 패턴을 찾은 위치의 반복자. 없으면 data.end().
 */
std::string_view::iterator FindPatternFromData(std::string_view data,
                                               std::string_view pattern);
/**
 * @brief 바이트 시퀀스에서 패턴이 처음 나타나는 위치를 찾는다.
 * @param data 검색 대상 바이트 시퀀스.
 * @param pattern 찾을 패턴.
 * @return 패턴을 찾은 위치의 반복자. 없으면 data.end().
 */
std::span<const std::uint8_t>::iterator FindPatternFromData(
    std::span<const std::uint8_t> data, std::span<const std::uint8_t> pattern);

}  // namespace bedrock::util