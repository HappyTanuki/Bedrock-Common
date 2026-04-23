#include "common/util/file.h"

int main() {
  std::string test = "test";
  bedrock::util::file::WriteToFile("test", test);
  test = bedrock::util::file::ReadEntireFileIntoString("test");
}
