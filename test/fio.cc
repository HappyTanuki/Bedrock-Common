#include "common/util/file.h"

int main() {
  std::string test = "test";
  bedrock::util::WriteToFile("test_file", test);
  if (test == bedrock::util::ReadEntireFileIntoString("test_file")) {
    return 0;
  }
  return -1;
}
