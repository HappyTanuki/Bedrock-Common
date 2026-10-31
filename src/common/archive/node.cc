#include "archive/node.h"

namespace bedrock::archive::transcriber {

std::int64_t FindIdentityError(const Node& node) noexcept {
  if (node.identity < 0) return node.identity;
  for (const Node& child : node.items) {
    const std::int64_t error = FindIdentityError(child);
    if (error < 0) return error;
  }
  for (const Node::Pair& pair : node.pairs) {
    const std::int64_t key_error = FindIdentityError(pair.key);
    if (key_error < 0) return key_error;
    const std::int64_t value_error = FindIdentityError(pair.value);
    if (value_error < 0) return value_error;
  }
  return 0;
}

}  // namespace bedrock::archive::transcriber
