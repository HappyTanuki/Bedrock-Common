#pragma once
#include <istream>

#include "transciber.h"

namespace bedrock::archive::transcriber {

struct Token {
  ValueType type;
  std::string token;
};

class YamlScanner {
 public:
  YamlScanner(std::istream& stream) : _stream(stream) {}

  Token Next();

  void Reset();

 private:
  void HandleChar(Token& tok, int c);
  std::size_t IndentSizeDiscovery();

  std::size_t _indent_size;
  std::size_t _indent_level = 0;
  bool _indent_confirmed = false;
  bool _red_eof = false;

  std::istream& _stream;
};

}  // namespace bedrock::archive::transcriber
