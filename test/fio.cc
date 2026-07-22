/**
 * @file fio.cc
 * @brief bedrock::util::WriteToFile / ReadEntireFile 문자열 왕복
 *        테스트.
 *
 * 문자열 "test"를 파일에 쓴 뒤 다시 읽어 들여 원본과 같은지 비교한다.
 *
 * @return 읽은 내용이 원본과 같으면 0, 다르면 -1.
 */
#include "common/util/file.h"

/** @brief "test" 문자열을 파일에 쓰고 다시 읽어 원본과 비교한다. */
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
