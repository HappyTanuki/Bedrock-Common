/**
 * @file parse.h
 * @brief YAML presentation stream을 serialization tree로 Parse한다.
 */
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "archive/yaml/serialization.h"

namespace bedrock::archive::yaml {

struct ParseResult {
  bool ok = false;
  std::string error;
  SerializationStream serialization;
};

/** @brief Presentation stream을 Parse해 presentation 비의존 serialization을
 * 만든다. */
ParseResult Parse(std::u32string_view presentation);
/** @brief 이미 UTF-32 decode된 presentation을 복사 없이 Parse한다. */
ParseResult Parse(std::span<const std::uint32_t> presentation);

/** @brief Parse 결과를 tree 대신 serialization sink로 직접 전달한다. */
bool ParseToSerializationSink(std::span<const std::uint32_t> presentation,
                              SerializationSink& sink, std::string& error);

}  // namespace bedrock::archive::yaml
