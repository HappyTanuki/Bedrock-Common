#pragma once
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <stack>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace bedrock::archive {

// ── 컨테이너 카테고리 concept ──
// map류: key_type + mapped_type 보유 (map/multimap/unordered_map/…)
template <typename C>
concept MapLike = std::ranges::range<C> && requires {
  typename C::key_type;
  typename C::mapped_type;  // set은 mapped_type 없음 → 자동 제외
};

// vector류: 임의 접근(index) + resize 되는 시퀀스 (vector/deque). string·blob
// 제외. (list/set 등은 추후 별도 concept로 확장)
template <typename C>
concept SeqLike =
    requires(C& c, std::size_t n) {
      typename C::value_type;
      c.resize(n);
      c[n];
    } && !MapLike<C> && !std::same_as<std::remove_cvref_t<C>, std::string> &&
    !std::same_as<typename C::value_type, std::byte>;

struct Schema;

class Visitor {
 public:
  Visitor(std::uint16_t machine_id) : _machine_id(machine_id) {}
  virtual ~Visitor();

  void operator()(Schema& root, std::string_view name = "");

  virtual void OnRootBegin(std::string_view name) = 0;
  virtual void OnRootEnd() = 0;

  virtual void OnObjectBegin(std::string_view name) = 0;
  virtual void OnObjectEnd() = 0;

  // ── 구조 훅 (컨테이너 일반화용) ──
  // 지연(트리/DOM) 모델: 개수를 미리 알 수 있으므로 OnSeqBegin/OnMapBegin이
  // 원소 수를 반환하고 그만큼 순회한다. 닫는 구분자/길이 back-patch는 트리
  // flush (begin_token/end_token·길이 계산)나 DOM 파싱이 처리하므로 별도 End
  // 콜백 불필요.
  virtual std::size_t OnSeqBegin(std::string_view name,
                                 std::size_t count) = 0;  // 반환=원소 수
  virtual void OnSeqEnd() = 0;

  virtual std::size_t OnMapBegin(std::string_view name,
                                 std::size_t count) = 0;  // 반환=엔트리 수
  virtual void OnMapEnd() = 0;
  // 키/값 역할·순서(키→값→키→값)는 직렬화기/역직렬화기의 상태 기계가 강제하므로
  // 별도 OnMapKey/OnMapValue 마커 훅이 필요 없다.

  // 방향 판별 (Serializer=false, Deserializer=true). map의 비대칭 처리에만
  // 사용.
  virtual bool IsReading() const = 0;

  // variant 역직렬화의 trial 지원(enum 없이 대안 선택).
  //  OnTrialBegin  : 대안을 시도하기 전 커서/상태 저장(savepoint).
  //  OnTrialCommit : 방금 시도한 대안이 노드 타입과 맞았으면 true(확정),
  //                  아니면 false(savepoint로 되돌리고 다음 대안 시도).
  // 직렬화기는 호출하지 않음. 역직렬화기가 파서 완성 시 구현한다.
  virtual void OnTrialBegin() {}
  virtual bool OnTrialCommit() { return true; }

  // ── 스칼라 리프 (포맷/방향별 구현) ──
  // clang-format off
  virtual Visitor& Visit(std::string_view name, bool& value) = 0;
  virtual Visitor& Visit(std::string_view name, std::byte& value) = 0;
  virtual Visitor& Visit(std::string_view name, std::int8_t& value) = 0;
  virtual Visitor& Visit(std::string_view name, std::uint8_t& value) = 0;
  virtual Visitor& Visit(std::string_view name, std::int16_t& value) = 0;
  virtual Visitor& Visit(std::string_view name, std::uint16_t& value) = 0;
  virtual Visitor& Visit(std::string_view name, std::int32_t& value) = 0;
  virtual Visitor& Visit(std::string_view name, std::uint32_t& value) = 0;
  virtual Visitor& Visit(std::string_view name, std::int64_t& value) = 0;
  virtual Visitor& Visit(std::string_view name, std::uint64_t& value) = 0;
  virtual Visitor& Visit(std::string_view name, float& value) = 0;
  virtual Visitor& Visit(std::string_view name, double& value) = 0;

  virtual Visitor& Visit(std::string_view name, std::vector<std::byte>& value) = 0;

  virtual Visitor& Visit(std::string_view name, std::string& value) = 0;
  // clang-format on

  // ── 제네릭 컨테이너/중첩 (베이스에 1번, 전 포맷·양방향 공용) ──

  // vector류(vector/deque). resize+제자리 방문이라 IsReading 분기 없이 양방향
  // 공용:
  //  직렬화 → n=크기, resize no-op, c[i] 기록
  //  역직렬화 → n=파싱된 개수, resize로 성장/축소, c[i] 채움
  // vector<byte>는 위 가상이 우선
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

  // map류(map/multimap/unordered_map/…). 키/값을 각각 Visit, 역할은
  // OnMapKey/Value로. 연관 컨테이너는 "생성+insert vs 순회" 비대칭이라
  // IsReading 분기가 불가피.
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

  // variant: 이종 값.
  //  직렬화 → 활성 대안을 그대로 Visit(std::visit로 디스패치).
  //  역직렬화 → 대안을 순서대로 "시도"해서 노드 타입과 맞는 첫 대안을
  //  채택(trial).
  template <typename... Ts>
  Visitor& Visit(std::string_view name, std::variant<Ts...>& v) {
    if (IsReading()) {
      (void)(TryAlternative<Ts>(name, v) || ...);  // 첫 성공에서 멈춤(단축평가)
    } else {
      std::visit([&](auto& alt) { Visit(name, alt); }, v);
    }
    return *this;
  }

  // 중첩 struct. OnObjectBegin → Accept → OnObjectEnd (visitor.cc에 정의)
  Visitor& Visit(std::string_view name, Schema& value);

 protected:
  // variant 대안 T를 한 번 시도한다. T로 emplace 후 읽어보고, OnTrialCommit이
  // 성공(true)이면 확정, 실패(false)면 되돌려져 다음 대안으로 넘어간다.
  template <typename T, typename... Ts>
  bool TryAlternative(std::string_view name, std::variant<Ts...>& v) {
    OnTrialBegin();
    Visit(name, v.template emplace<T>());
    return OnTrialCommit();
  }

  // ── State 패턴 프레임워크 (전 포맷·양방향 공유) ──
  // 상태 인터페이스와 상태 스택을 베이스에 둔다. 구체 상태(전이·동작)는 각 포맷의
  // 직렬화기/역직렬화기가 이 State를 상속(확장)해 정의한다. 값 페이로드(문자열·
  // ValueType 등)는 포맷별이라 베이스 인터페이스에 싣지 않고, 파생 상태가 자기
  // 구체 클래스로 다운캐스트해 접근한다.
  struct State {
    virtual ~State();
    virtual void OnScalar(Visitor& v) const = 0;
    virtual std::string ResolveName(Visitor& v, std::string_view name) const = 0;
  };
  const State& CurrentState() const { return *_state_stack.top(); }
  void PushState(const State& s) { _state_stack.push(&s); }
  void PopState() { _state_stack.pop(); }
  void TransitionTo(const State& s) { _state_stack.top() = &s; }

  std::uint16_t _machine_id;

  std::size_t _indent_size = 2;

  std::stack<const State*> _state_stack;
};

}  // namespace bedrock::archive
