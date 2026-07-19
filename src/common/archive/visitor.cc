#include "common/archive/visitor.h"

#include "common/archive.h"

namespace bedrock::archive {

Visitor::~Visitor() = default;

void Visitor::operator()(Schema& root, std::string_view name) {
  OnRootBegin(name);
  root.Accept(*this);
  OnRootEnd();
}

}  // namespace bedrock::archive
