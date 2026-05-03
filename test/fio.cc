#include "common/util/file.h"

int main() {
  std::string test = "test";
  std::string content = "";
  bedrock::util::WriteToFile("test_file", test);
  bedrock::util::ReadEntireFile("test_file", content);
  if (test == content) {
    return 0;
  }
  return -1;
}
