/**
 * @file present.h
 * @brief YAML serialization tree를 character stream으로 Present한다.
 */
#pragma once

#include <string>

#include "archive/yaml/serialization.h"

namespace bedrock::archive::yaml {

struct PresentResult {
  bool ok = false;
  std::string error;
  std::string text;
};

/** @brief Serialization에 presentation 세부사항을 선택해 character stream을
 * 만든다. */
PresentResult Present(const SerializationStream& serialization);

}  // namespace bedrock::archive::yaml
