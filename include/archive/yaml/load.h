/**
 * @file load.h
 * @brief YAML Load = Parse + Compose.
 */
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "archive/yaml/compose.h"

namespace bedrock::archive::yaml {

/** @brief Character stream을 Parse한 뒤 representation으로 Compose한다. */
ComposeResult Load(std::u32string_view presentation,
                   const ComposeOptions& options = {});
/** @brief 이미 UTF-32 decode된 character stream을 복사 없이 Load한다. */
ComposeResult Load(std::span<const std::uint32_t> presentation,
                   const ComposeOptions& options = {});

}  // namespace bedrock::archive::yaml
