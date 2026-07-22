/**
 * @file visitor.h
 * @brief Visitor 패턴 기반 클래스와 컨테이너 인식용 concept 정의.
 *
 * 실제 포맷(YAML 등)에 의존하지 않는 순회 로직의 핵심으로, 스칼라 리프는
 * 포맷별 파생 클래스가 구현하고, 컨테이너(SeqLike/MapLike)·variant·중첩
 * Schema에 대한 순회는 이 베이스 클래스에 한 번만 구현되어 공유된다.
 */
#pragma once
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace bedrock::archive {

// ── 컨테이너 카테고리 concept ──
/**
 * @brief map류 컨테이너를 식별하는 concept.
 *
 * key_type과 mapped_type을 모두 갖는 타입(map/multimap/unordered_map
 * 등)에 대해 성립한다. set 계열은 mapped_type이 없으므로 자동으로
 * 제외된다.
 */
template <typename C>
concept MapLike = std::ranges::range<C> && requires {
  typename C::key_type;
  typename C::mapped_type;  // set은 mapped_type 없음 → 자동 제외
};

/**
 * @brief vector류 컨테이너를 식별하는 concept.
 *
 * 임의 접근(index)과 resize가 가능한 시퀀스(vector/deque)에 대해
 * 성립한다. string과 바이트 blob(vector<byte>)은 제외한다.
 */
template <typename C>
concept SeqLike =
    requires(C& c, std::size_t n) {
      typename C::value_type;
      c.resize(n);
      c[n];
    } && !MapLike<C> && !std::same_as<std::remove_cvref_t<C>, std::string> &&
    !std::same_as<typename C::value_type, std::byte>;

/**
 * @brief set류 컨테이너를 식별하는 concept.
 *
 * key_type은 있으나 mapped_type이 없고 insert가 가능한 컨테이너
 * (set/unordered_set/multiset 등)에 대해 성립한다. 표현 트리에서는
 * 집합(Node::Kind::kSet)으로 나타난다.
 */
template <typename C>
concept SetLike = std::ranges::range<C> &&
                  requires(C& c, typename C::value_type v) {
                    typename C::key_type;
                    c.insert(std::move(v));
                  } && !MapLike<C>;

/** @brief Schema 전방 선언(정의는 common/archive.h 참고). */
struct Schema;

/**
 * @brief 아카이브 트리를 순회하며 값을 읽거나 쓰는 Visitor 패턴의
 * 기반 클래스.
 *
 * Schema::Accept()를 통해 사용자 데이터 구조를 방문하며, 스칼라/컨테이너/
 * 중첩 객체에 대한 방문 로직을 제공한다. 컨테이너(SeqLike/MapLike)와
 * variant, 중첩 Schema에 대한 Visit()는 이 베이스 클래스에 한 번만
 * 구현되어 모든 포맷(직렬화기/역직렬화기)이 공유하며, 포맷별 클래스는
 * 스칼라 리프 방문과 OnXxxBegin/End 훅만 구현하면 된다.
 */
class Visitor {
 public:
  /**
   * @brief machine_id로 Visitor를 생성한다.
   * @param machine_id Snowflake ID 생성에 쓰이는 머신 식별자.
   */
  Visitor(std::uint16_t machine_id) : _machine_id(machine_id) {}
  /** @brief 가상 소멸자. */
  virtual ~Visitor();

  /**
   * @brief 순회 진입점. 루트 스키마를 방문한다.
   * @param root 방문할 최상위 Schema.
   * @param name 루트 이름(생략 시 빈 문자열).
   */
  void operator()(Schema& root, std::string_view name = "");

  /** @brief 최상위 루트 진입을 알린다. */
  virtual void OnRootBegin(std::string_view name) = 0;
  /** @brief 최상위 루트 종료를 알린다. */
  virtual void OnRootEnd() = 0;

  /** @brief 중첩 객체 진입을 알린다. */
  virtual void OnObjectBegin(std::string_view name) = 0;
  /** @brief 중첩 객체 종료를 알린다. */
  virtual void OnObjectEnd() = 0;

  // ── 구조 훅 (컨테이너 일반화용) ──
  /**
   * @brief 시퀀스 진입을 알린다.
   *
   * 지연(트리/DOM) 모델에서는 개수를 미리 알 수 있으므로, 이 함수가
   * 원소 수를 반환하면 호출자가 그만큼 순회한다. 닫는 구분자/길이의
   * back-patch는 트리 flush(begin_token/end_token·길이 계산)나 DOM
   * 파싱이 처리하므로 별도의 End 콜백 인자는 필요 없다.
   * @param name 시퀀스 필드 이름.
   * @param count 원소 개수(직렬화 시 알고 있는 크기의 힌트).
   * @return 실제로 순회할 원소 수.
   */
  virtual std::size_t OnSeqBegin(std::string_view name,
                                 std::size_t count) = 0;  // 반환=원소 수
  /** @brief 시퀀스 종료를 알린다. */
  virtual void OnSeqEnd() = 0;

  /**
   * @brief 맵 진입을 알린다.
   * @param name 맵 필드 이름.
   * @param count 엔트리 개수(직렬화 시 알고 있는 크기의 힌트).
   * @return 실제로 순회할 엔트리 수.
   * @note 키/값 역할·순서(키→값→키→값)는 직렬화기/역직렬화기의 상태
   * 기계가 강제하므로 별도 OnMapKey/OnMapValue 마커 훅이 필요 없다.
   */
  virtual std::size_t OnMapBegin(std::string_view name,
                                 std::size_t count) = 0;  // 반환=엔트리 수
  /** @brief 맵 종료를 알린다. */
  virtual void OnMapEnd() = 0;

  /**
   * @brief 집합 진입을 알린다.
   * @param name 집합 필드 이름.
   * @param count 원소 개수(직렬화 시 알고 있는 크기의 힌트).
   * @return 실제로 순회할 원소 수.
   */
  virtual std::size_t OnSetBegin(std::string_view name,
                                 std::size_t count) = 0;  // 반환=원소 수
  /** @brief 집합 종료를 알린다. */
  virtual void OnSetEnd() = 0;

  /**
   * @brief 방향(직렬화/역직렬화)을 판별한다.
   * @note map의 "생성+insert vs 순회" 비대칭 처리에만 사용된다.
   * @return Serializer는 false, Deserializer는 true.
   */
  virtual bool IsReading() const = 0;

  /**
   * @brief variant 역직렬화의 trial(시행) 시작 — 대안을 시도하기 전
   * 커서/상태를 저장한다(savepoint).
   * @note 직렬화기는 호출하지 않는다. 역직렬화기가 파서 완성 시
   * 구현한다.
   */
  virtual void OnTrialBegin() {}
  /**
   * @brief variant 역직렬화의 trial(시행) 확정 — 방금 시도한 대안이
   * 노드 타입과 맞았는지를 반환한다.
   * @return 맞았으면 true(확정). 아니면 false(savepoint로 되돌리고
   * 다음 대안을 시도).
   * @note 직렬화기는 호출하지 않는다. 역직렬화기가 파서 완성 시
   * 구현한다.
   */
  virtual bool OnTrialCommit() { return true; }

  // ── 스칼라 리프 (포맷/방향별 구현) ──
  // clang-format off
  /** @brief bool 값을 방문한다. */
  virtual Visitor& Visit(std::string_view name, bool& value) = 0;
  /** @brief byte 값을 방문한다. */
  virtual Visitor& Visit(std::string_view name, std::byte& value) = 0;
  /** @brief int8_t 값을 방문한다. */
  virtual Visitor& Visit(std::string_view name, std::int8_t& value) = 0;
  /** @brief uint8_t 값을 방문한다. */
  virtual Visitor& Visit(std::string_view name, std::uint8_t& value) = 0;
  /** @brief int16_t 값을 방문한다. */
  virtual Visitor& Visit(std::string_view name, std::int16_t& value) = 0;
  /** @brief uint16_t 값을 방문한다. */
  virtual Visitor& Visit(std::string_view name, std::uint16_t& value) = 0;
  /** @brief int32_t 값을 방문한다. */
  virtual Visitor& Visit(std::string_view name, std::int32_t& value) = 0;
  /** @brief uint32_t 값을 방문한다. */
  virtual Visitor& Visit(std::string_view name, std::uint32_t& value) = 0;
  /** @brief int64_t 값을 방문한다. */
  virtual Visitor& Visit(std::string_view name, std::int64_t& value) = 0;
  /** @brief uint64_t 값을 방문한다. */
  virtual Visitor& Visit(std::string_view name, std::uint64_t& value) = 0;
  /** @brief float 값을 방문한다. */
  virtual Visitor& Visit(std::string_view name, float& value) = 0;
  /** @brief double 값을 방문한다. */
  virtual Visitor& Visit(std::string_view name, double& value) = 0;

  /** @brief 바이트열(blob) 값을 방문한다. */
  virtual Visitor& Visit(std::string_view name, std::vector<std::byte>& value) = 0;

  /** @brief 문자열 값을 방문한다. */
  virtual Visitor& Visit(std::string_view name, std::string& value) = 0;
  // clang-format on

  // ── 제네릭 컨테이너/중첩 (베이스에 1번, 전 포맷·양방향 공용) ──

  /**
   * @brief vector류(vector/deque) 컨테이너를 방문한다.
   *
   * resize 후 제자리에서 원소를 방문하므로 IsReading 분기 없이 양방향
   * 공용으로 동작한다.
   *  - 직렬화: n=현재 크기, resize는 no-op, c[i]를 기록.
   *  - 역직렬화: n=파싱된 개수, resize로 성장/축소 후 c[i]를 채움.
   * @param name 필드 이름.
   * @param c 방문할 컨테이너(SeqLike).
   * @return *this.
   * @note vector<std::byte>는 전용 가상 Visit 오버로드가 우선한다.
   */
  template <SeqLike C>
  Visitor& Visit(std::string_view name, C& c) {
    std::size_t n = OnSeqBegin(name, c.size());
    if (n == std::numeric_limits<std::size_t>::max()) {
      return *this;
    }
    c.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
      Visit(std::string_view{}, c[i]);  // 재귀 — 원소는 이름 없음
    }
    OnSeqEnd();
    return *this;
  }

  /**
   * @brief map류(map/multimap/unordered_map 등) 컨테이너를 방문한다.
   *
   * 키/값을 각각 Visit()로 순회하며, 키·값 역할 구분은 포맷별 State가
   * 담당한다(예: YAMLSerializer의 MapKeyState/MapValueState). 연관
   * 컨테이너는 "생성+insert vs 순회"라는 비대칭 구조를 가지므로
   * IsReading() 분기가 불가피하다.
   * @param name 필드 이름.
   * @param m 방문할 컨테이너(MapLike).
   * @return *this.
   */
  template <MapLike C>
  Visitor& Visit(std::string_view name, C& m) {
    using K = typename C::key_type;
    using V = typename C::mapped_type;
    std::size_t n = OnMapBegin(name, m.size());
    if (n == std::numeric_limits<std::size_t>::max()) {
      return *this;  // 실패/미구현 센티널 (seq와 동일) → 무한 루프 방지
    }
    // 키→값→키→값 순서/역할은 상태 기계가 강제하므로 마커 호출이 필요 없다.
    if (IsReading()) {
      m.clear();
      for (std::size_t i = 0; i < n; ++i) {
        K k{};
        V v{};
        Visit(std::string_view{}, k);  // 상태: 키 처리 → 값으로 전이
        Visit(std::string_view{}, v);  // 상태: 값 처리 → 키로 전이
        m.insert({std::move(k), std::move(v)});  // multimap도 OK
      }
    } else {
      for (auto& [k, val] : m) {
        K key = k;  // 맵의 키는 const → 방문 위해 복사
        Visit(std::string_view{}, key);
        Visit(std::string_view{}, val);
      }
    }
    OnMapEnd();
    return *this;
  }

  /**
   * @brief set류(set/unordered_set/multiset 등) 컨테이너를 방문한다.
   *
   * 원소는 불변(키)이므로 시퀀스처럼 제자리 방문이 불가능하다 —
   * 읽기는 "기본 생성 → 방문 → insert", 쓰기는 원소를 복사해 방문한다.
   * @param name 필드 이름.
   * @param c 방문할 컨테이너(SetLike).
   * @return *this.
   */
  template <SetLike C>
  Visitor& Visit(std::string_view name, C& c) {
    using K = typename C::value_type;
    std::size_t n = OnSetBegin(name, c.size());
    if (n == std::numeric_limits<std::size_t>::max()) {
      return *this;  // 실패/미구현 센티널 → 무한 루프 방지
    }
    if (IsReading()) {
      c.clear();
      for (std::size_t i = 0; i < n; ++i) {
        K k{};
        Visit(std::string_view{}, k);
        c.insert(std::move(k));  // multiset도 OK
      }
    } else {
      for (const auto& e : c) {
        K copy = e;  // set 원소는 const → 방문 위해 복사
        Visit(std::string_view{}, copy);
      }
    }
    OnSetEnd();
    return *this;
  }

  /**
   * @brief variant(이종 값)를 방문한다.
   *
   *  - 직렬화: 현재 활성 대안을 그대로 방문한다(std::visit로 디스패치).
   *  - 역직렬화: 대안을 순서대로 "시도"하여 노드 타입과 맞는 첫 대안을
   *    채택한다(trial).
   * @param name 필드 이름.
   * @param v 방문할 std::variant.
   * @return *this.
   */
  template <typename... Ts>
  Visitor& Visit(std::string_view name, std::variant<Ts...>& v) {
    if (IsReading()) {
      (void)(TryAlternative<Ts>(name, v) || ...);  // 첫 성공에서 멈춤(단축평가)
    } else {
      std::visit([&](auto& alt) { Visit(name, alt); }, v);
    }
    return *this;
  }

  /**
   * @brief 중첩 Schema(구조체)를 방문한다.
   *
   * OnObjectBegin → Schema::Accept → OnObjectEnd 순으로 호출한다.
   * 정의는 visitor.cc 참고.
   * @param name 필드 이름.
   * @param value 방문할 중첩 Schema.
   * @return *this.
   */
  Visitor& Visit(std::string_view name, Schema& value);

 protected:
  /**
   * @brief variant 대안 T를 한 번 시도한다.
   *
   * T로 emplace한 뒤 읽어보고, OnTrialCommit()이 성공(true)이면
   * 확정하고, 실패(false)면 (savepoint로) 되돌려져 다음 대안으로
   * 넘어간다.
   * @tparam T 시도할 대안 타입.
   * @tparam Ts variant가 가질 수 있는 전체 대안 타입 목록.
   * @param name 필드 이름.
   * @param v 대상 std::variant.
   * @return 이 대안이 채택되었으면 true.
   */
  template <typename T, typename... Ts>
  bool TryAlternative(std::string_view name, std::variant<Ts...>& v) {
    OnTrialBegin();
    Visit(name, v.template emplace<T>());
    return OnTrialCommit();
  }

  /** @brief Snowflake ID 생성에 쓰이는 머신 식별자. */
  std::uint16_t _machine_id;

  /** @brief 포맷 출력 시 사용하는 들여쓰기 폭(공백 수). */
  std::size_t _indent_size = 2;
};

}  // namespace bedrock::archive
