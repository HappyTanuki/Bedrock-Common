#include "common/util/file.h"

int main() {
  std::string filename = "find_pattern.pdb";

  std::string pattern1 = "text";
  auto offset1 = bedrock::util::FindFirstApperenceFromFile(filename, pattern1);

  std::vector<std::uint8_t> pattern2 = {'t', 'e', 'x', 't'};
  auto offset2 = bedrock::util::FindFirstApperenceFromFile(filename, pattern2);

  if (offset1 == offset2) {
    return 0;
  }

  return -1;
}
