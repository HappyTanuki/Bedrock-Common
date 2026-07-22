/**
 * @file node.h
 * @brief 포맷 중립 표현(Representation) 트리 노드.
 *
 * 스펙(YAML 3.1)의 Representation 모델에 대응하는, 트랜스크라이버
 * 코어와 포맷 계층(yaml 등) 사이의 교환 타입이다. 포맷별 파서(Compose)가
 * 이 트리를 만들고, 역직렬화기(Construct)가 이 트리에서 값을 꺼낸다.
 * 제시(Presentation) 정보 — 따옴표·들여쓰기·토큰 — 는 담지 않는다.
 */
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bedrock::archive::transcriber {

/**
 * @brief 스칼라/컨테이너 값의 종류를 나타내는 비트 플래그.
 *
 * operator|로 여러 플래그를 조합할 수 있다(예: kSequence | kString).
 * 표현 트리에서는 스칼라 노드의 쓰기 힌트(Node::vtype)로 쓰인다.
 */
enum class ValueType : std::uint32_t {
  /** @brief 값 없음. */
  kNull = 0,
  /** @brief 시퀀스(배열) 컨테이너. */
  kSequence = 1,
  /** @brief 맵(연관 배열) 컨테이너. */
  kMapping = 1 << 1,
  /**
   * @brief 집합 컨테이너.
   * @note 대응하는 concept·처리 로직이 아직 없다(예약된 값, 미사용).
   */
  kSet = 1 << 2,
  /** @brief 문자열 값. */
  kString = 1 << 3,
  /** @brief 숫자 값. */
  kNumber = 1 << 4,
  /** @brief 불리언 값. */
  kBoolean = 1 << 5,
  /** @brief 바이너리(바이트열) 값. */
  kBinary = 1 << 6
};

/**
 * @brief 두 ValueType 플래그를 비트 OR로 합친다.
 * @param a 첫 번째 플래그.
 * @param b 두 번째 플래그.
 * @return 합쳐진 플래그.
 */
constexpr ValueType operator|(ValueType a, ValueType b) {
  return static_cast<ValueType>(static_cast<std::uint32_t>(a) |
                                static_cast<std::uint32_t>(b));
}
/**
 * @brief ValueType 플래그를 비트 OR로 누적한다.
 * @param a 누적 대상(결과가 대입된다).
 * @param b 추가할 플래그.
 * @return 갱신된 a에 대한 참조.
 */
constexpr ValueType& operator|=(ValueType& a, ValueType b) { return a = a | b; }
/**
 * @brief 두 ValueType 플래그를 비트 AND로 결합한다.
 * @param a 첫 번째 플래그.
 * @param b 두 번째 플래그.
 * @return 공통으로 켜진 플래그만 남은 값.
 */
constexpr ValueType operator&(ValueType a, ValueType b) {
  return static_cast<ValueType>(static_cast<std::uint32_t>(a) &
                                static_cast<std::uint32_t>(b));
}
/**
 * @brief 플래그가 켜져 있는지 확인한다.
 * @param v 검사 대상 값.
 * @param f 확인할 플래그.
 * @return 플래그가 하나라도 켜져 있으면 true.
 */
constexpr bool HasFlag(ValueType v, ValueType f) {
  return (static_cast<std::uint32_t>(v) & static_cast<std::uint32_t>(f)) != 0;
}

/**
 * @brief 표현 트리 노드 하나(스칼라 | 시퀀스 | 매핑 | 집합).
 *
 * 포맷 중립을 위한 표현력:
 *  - 매핑은 키도 Node이므로(Pair) 복합 키를 표현할 수 있고, 항목 순서를
 *    보존하며, **중복 키를 허용**한다(multimap류의 표현 — 유일성 강제는
 *    Native 타입의 몫).
 *  - 집합(kSet)은 시퀀스와 같은 저장(items)을 쓰되 "원소 유일·무순서"
 *    라는 의미를 남긴다. 표기가 없는 포맷은 시퀀스처럼 제시해도 된다
 *    (예: YAML은 시퀀스로 렌더, !!set 표기는 후속).
 *  - 별칭(*alias)은 조립 단계에서 값 복사로 해소된다.
 * @note Pair는 자기 참조 타입이라 전방 선언 후 Node 정의 밖에서
 * 완성한다(std::pair는 불완전 타입을 지원하지 않음).
 */
struct Node {
  /** @brief 매핑 항목 하나(키, 값) — 정의는 Node 아래. */
  struct Pair;

  /** @brief 노드의 종류. */
  enum class Kind : std::uint8_t {
    /** @brief 스칼라(문자열 값, null 포함). */
    kScalar,
    /** @brief 시퀀스(items, 순서 보존). */
    kSequence,
    /** @brief 매핑(pairs, 순서 보존, 중복 키 허용). */
    kMapping,
    /** @brief 집합(items, 원소 유일·무순서 의미). */
    kSet,
  };

  /** @brief 노드의 종류. */
  Kind kind = Kind::kScalar;
  /** @brief 빈(e-node) 스칼라 여부 — ""(빈 문자열)과 null을 구분한다. */
  bool null = false;
  /** @brief 태그 원문("!!binary" 등). 없으면 빈 문자열. */
  std::string tag;
  /**
   * @brief 스칼라의 쓰기 힌트 — Present가 렌더 방식(plain/따옴표/
   * !!binary 블록)을 고를 때 쓴다. 읽기(Construct)는 목표 C++ 타입으로
   * 해소하므로 이 값을 보지 않는다. kNull은 "plain으로 그대로"(필드
   * 이름 키 등).
   */
  ValueType vtype = ValueType::kNull;
  /** @brief kScalar: 디코드된 값(UTF-8). 이스케이프·접힘·청킹 적용 후. */
  std::string scalar;
  /** @brief kSequence/kSet: 원소들. */
  std::vector<Node> items;
  /**
   * @brief kMapping: (키, 값) 쌍들 — 키도 Node(복합 키 지원).
   * 중복 키가 나타날 수 있다(multimap류) — 순서대로 보존된다.
   */
  std::vector<Pair> pairs;

  /**
   * @brief 스칼라 키로 매핑에서 값을 찾는다(복합 키는 대상 아님).
   * 중복 키면 첫 번째 항목을 돌려준다.
   * @param key 찾을 키 문자열.
   * @return 찾은 값 노드, 없으면 nullptr.
   */
  const Node* Find(std::string_view key) const;
};

/** @brief 매핑 항목 하나 — 키/값 모두 완전한 Node. */
struct Node::Pair {
  /** @brief 매핑 키(스칼라뿐 아니라 컬렉션도 가능). */
  Node key;
  /** @brief 매핑 값. */
  Node value;
};

inline const Node* Node::Find(std::string_view key) const {
  for (const Pair& p : pairs) {
    if (p.key.kind == Kind::kScalar && !p.key.null && p.key.scalar == key) {
      return &p.value;
    }
  }
  return nullptr;
}

}  // namespace bedrock::archive::transcriber
