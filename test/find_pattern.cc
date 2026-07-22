/**
 * @file find_pattern.cc
 * @brief bedrock::util::FindFirstAppearanceFromFile 두 오버로드
 *        일치 테스트.
 *
 * 픽스처 파일 "find_pattern.pdb"에서 같은 내용을 각각
 * std::string_view 패턴("text")과 std::uint8_t 바이트 패턴으로
 * 검색해, 두 오버로드가 같은 첫 출현 오프셋을 돌려주는지 비교한다.
 *
 * @note 픽스처 파일 "find_pattern.pdb"는 이 테스트가 만들지 않으며,
 *       실행 전 작업 디렉터리에 미리 준비되어 있어야 한다.
 * @return 두 오프셋이 같으면 0, 다르면 -1.
 */
#include "common/util/file.h"

/** @brief 문자열 패턴과 동일 바이트 패턴의 검색 오프셋을 비교한다. */
int main() {
  std::string filename = "find_pattern.pdb";

  std::string pattern1 = "text";
  auto offset1 = bedrock::util::FindFirstAppearanceFromFile(filename, pattern1);

  std::vector<std::uint8_t> pattern2 = {'t', 'e', 'x', 't'};
  auto offset2 = bedrock::util::FindFirstAppearanceFromFile(filename, pattern2);

  if (offset1 == offset2) {
    return 0;
  }

  return -1;
}
