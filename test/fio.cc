#include "common/util/file.h"

int main() {
  std::string test = "test";
  bedrock::util::WriteToFile("test", test);
  if (test == bedrock::util::ReadEntireFile("test")) {
    return 0;
  }
  return -1;
}
