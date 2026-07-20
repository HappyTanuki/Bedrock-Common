#include "common/archive/transcriber/yaml_scanner.h"

#include <limits>

namespace bedrock::archive::transcriber {

Token YamlScanner::Next() {
  Token tok;
  if (_red_eof) {
    return tok;
  }
  int target_c = std::numeric_limits<int>::max();
  int c = 0;
  for (c = _stream.get(); c != std::char_traits<char>::eof();
       c = _stream.get()) {
    if (c == '#' || c != target_c) {
      target_c = '\n';
      continue;
    } else if (c == target_c) {
      target_c = std::numeric_limits<int>::max();
      continue;
    }
    HandleChar(tok, c);
  }
  if (c == std::char_traits<char>::eof()) {
    _red_eof = true;
  }

  return tok;
}

void YamlScanner::Reset() {}

void YamlScanner::HandleChar(Token& tok, int c) {
  switch (c) {
    case ' ': {
      if (!_indent_confirmed) {
        _indent_size++;
        return;
      }
    } break;
    case ':': {
      if (!_indent_confirmed) {
        _indent_size++;
        return;
      }
    } break;
    default: {
      if (!_indent_confirmed) {
        _indent_confirmed = true;
      }
      tok.token += c;
    } break;
  }
}

std::size_t YamlScanner::IndentSizeDiscovery() {}

}  // namespace bedrock::archive::transcriber
