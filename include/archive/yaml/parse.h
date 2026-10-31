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

/** @brief YAML presentation을 Parse해 presentation 비의존 serialization tree를
 * 만든다. */
ParseResult Parse(std::u32string_view presentation);
/** @brief 이미 UTF-32 decode된 presentation을 복사 없이 Parse한다. */
ParseResult Parse(std::span<const std::uint32_t> presentation);
/** @brief Parse event를 presentation 비의존 serialization sink에 전달한다. */
bool Parse(std::span<const std::uint32_t> presentation,
           SerializationSink& output, std::string& error);

}  // namespace bedrock::archive::yaml
