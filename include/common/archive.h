#pragma once

#include "archive/visitor.h"

namespace bedrock::archive {

struct Schema {
  virtual ~Schema();
  virtual void Accept(Visitor& visitor) = 0;
};

}  // namespace bedrock::archive
