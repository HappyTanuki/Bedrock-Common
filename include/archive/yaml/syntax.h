/**
 * @file syntax.h
 * @brief YAML 문법 분석 결과인 경량 구문 트리 자료형.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bedrock::archive::yaml {

/** @brief 스칼라 표기 스타일(값 디코드 방식을 결정). */
enum class ScalarStyle : std::uint8_t {
  kPlain,
  kSingleQuoted,
  kDoubleQuoted,
  kLiteral,
  kFolded,
};

/** @brief 블록 스칼라의 chomping 모드([164] c-chomping-indicator). */
enum class ChompKind : std::uint8_t {
  kStrip,
  kClip,
  kKeep,
};

/** @brief 문법 인식기가 만드는 구문 트리 노드 종류. */
enum class SyntaxKind : std::uint8_t {
  kDocument,
  kMapping,
  kSequence,
  kScalar,
  kAlias,
  kAnchor,
  kTag,
};

/** @brief 입력 구간과 자식 노드를 소유하는 YAML 경량 구문 트리 노드. */
struct SyntaxNode {
  SyntaxKind kind = SyntaxKind::kDocument;
  ScalarStyle style = ScalarStyle::kPlain;
  ChompKind chomp = ChompKind::kClip;
  std::ptrdiff_t indent = 0;
  std::size_t begin = 0;
  std::size_t end = 0;
  std::vector<SyntaxNode> children;
};

}  // namespace bedrock::archive::yaml
