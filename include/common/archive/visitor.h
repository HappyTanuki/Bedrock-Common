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
#include <span>
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
  typename C::mapped_type;  // set은 mapped_type 없음 -> 자동 제외
};

/**
 * @brief 중복 키를 허용하는 map류(multimap 계열)를 식별하는 concept.
 *
 * key_type·mapped_type에 더해 insert가 항상 성립(중복 허용)하는
 * multi-key 연관 컨테이너다. 역직렬화기는 이 컨테이너를 만날 때만
 * 포맷 방언의 중복 키 관대함을 켠다.
 */
template <typename C>
concept MultiMapLike =
    MapLike<C> && requires(C& container, typename C::value_type entry) {
      { container.insert(entry) } -> std::same_as<typename C::iterator>;
    };

/**
 * @brief vector류 컨테이너를 식별하는 concept.
 *
 * 임의 접근(index)과 resize가 가능한 시퀀스(vector/deque)에 대해
 * 성립한다. string과 바이트 blob(vector<byte>)은 제외한다.
 */
template <typename C>
concept SeqLike =
    requires(C& container, std::size_t element_count) {
      typename C::value_type;
      container.resize(element_count);
      container[element_count];
    } && !MapLike<C> && !std::same_as<std::remove_cvref_t<C>, std::string> &&
    !std::same_as<typename C::value_type, std::byte>;

/**
 * @brief set류 컨테이너를 식별하는 concept.
 *
 * key_type은 있으나 mapped_type이 없고 insert가 가능한 컨테이너
 * (set/unordered_set/multiset 등)에 대해 성립한다. 표현 트리에서는
 * 직렬화 포맷에서는 집합 시퀀스로 표현되며 API는 그 내부 표현 타입을 노출하지
 * 않는다.
 */
template <typename C>
concept SetLike = std::ranges::range<C> &&
                  requires(C& container, typename C::value_type value) {
                    typename C::key_type;
                    container.insert(std::move(value));
                  } && !MapLike<C>;

/** @brief Schema 전방 선언(정의는 common/archive.h 참고). */
struct Schema;

/**
 * @brief 스칼라 필드의 와이어 인코딩 힌트.
 *
 * C++ 타입만으로 결정되지 않는 인코딩 변형을 고른다(주로 스키마 구동
 * 바이너리 포맷용). 자기서술 포맷(YAML/JSON 등)은 항상 kAuto로 취급한다.
 */
enum class WireHint : std::uint8_t {
  kAuto,    ///< C++ 타입 기본 인코딩(자기서술 포맷은 항상 이것).
  kZigzag,  ///< protobuf sint(부호 varint, zigzag).
  kFixed,   ///< protobuf fixed32/64·sfixed(고정폭).
};

/**
 * @brief 필드 식별자 + 인코딩 힌트를 함께 나르는 방문 대상 기술자.
 *
 * 이름(자기서술 키)과 번호(protobuf 필드번호/ASN.1 태그)를 한 호출에 실어,
 * 하나의 Accept()가 자기서술·스키마구동 백엔드를 동시에 섬기게 한다.
 * 자기서술 포맷은 name만, 스키마구동 바이너리는 number/wire를 본다.
 *
 * const char* 나 std::string_view에서 암묵 변환되므로 기존 Visit("name", v)
 * 호출부는 그대로 동작하고, std::string_view로도 암묵 변환되어 이름 기반
 * 내부 로직(필드 조회 등)에 값 변경 없이 쓰인다.
 */
struct Field {
  std::string_view name;            ///< 필드 이름(무명 원소는 "").
  std::uint32_t number = 0;         ///< protobuf 필드번호 / ASN.1 태그(0=없음).
  WireHint wire = WireHint::kAuto;  ///< 스칼라 와이어 힌트.

  constexpr Field() = default;
  constexpr explicit Field(const char* element_count)
      : name(element_count) {}  // NOLINT(google-explicit-constructor)
  constexpr explicit Field(std::string_view element_count)
      : name(element_count) {}  // NOLINT(google-explicit-constructor)
  constexpr Field(std::string_view element_count, std::uint32_t num,
                  WireHint wire_hint = WireHint::kAuto)
      : name(element_count), number(num), wire(wire_hint) {}
  constexpr explicit operator std::string_view()
      const {  // NOLINT(google-explicit-constructor)
    return name;
  }
};

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
  Visitor(const Visitor&) = delete;
  Visitor& operator=(const Visitor&) = delete;
  Visitor(Visitor&&) = delete;
  Visitor& operator=(Visitor&&) = delete;
  /**
   * @brief machine_id로 Visitor를 생성한다.
   * @param machine_id Snowflake ID 생성에 쓰이는 머신 식별자.
   */
  explicit Visitor(std::uint16_t machine_id) : machine_id_(machine_id) {}
  /** @brief 가상 소멸자. */
  virtual ~Visitor();

  /**
   * @brief 순회 진입점. 루트 스키마를 방문한다.
   * @param root 방문할 최상위 Schema.
   * @param name 루트 이름(생략 시 빈 문자열).
   */
  void operator()(Schema& root, std::string_view name = "");

  /** @brief 최상위 루트 진입을 알린다. */
  virtual void OnRootBegin(const Field& name) = 0;
  /** @brief 최상위 루트 종료를 알린다. */
  virtual void OnRootEnd() = 0;

  /** @brief 중첩 객체 진입을 알린다. */
  virtual void OnObjectBegin(const Field& name) = 0;
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
  virtual std::size_t OnSeqBegin(const Field& name,
                                 std::size_t count) = 0;  // 반환=원소 수
  /** @brief 시퀀스 종료를 알린다. */
  virtual void OnSeqEnd() = 0;

  /**
   * @brief 맵 진입을 알린다.
   * @param name 맵 필드 이름.
   * @param count 엔트리 개수(직렬화 시 알고 있는 크기의 힌트).
   * @return 실제로 순회할 엔트리 수.
   * @note 키/값 역할·순서(키->값->키->값)는 직렬화기/역직렬화기의 상태
   * 기계가 강제하므로 별도 OnMapKey/OnMapValue 마커 훅이 필요 없다.
   */
  virtual std::size_t OnMapBegin(const Field& name,
                                 std::size_t count) = 0;  // 반환=엔트리 수
  /** @brief 맵 종료를 알린다. */
  virtual void OnMapEnd() = 0;

  /**
   * @brief 중복 키 허용 맵(multimap 계열) 진입을 알린다.
   *
   * Visit(map)이 MultiMapLike 컨테이너를 만났을 때만 호출된다. 포맷
   * 구현은 이 신호를 받아 해당 매핑에 한해 중복 키 검증을 완화한다
   * (예: YAML의 duplicate-key 거부 해제). 기본 구현은 아무 것도 하지
   * 않는다 — 중복 키 개념이 없는 포맷(RBF 등)은 무시하면 된다.
   */
  virtual void OnDuplicateKeysBegin() {}

  /**
   * @brief 집합 진입을 알린다.
   * @param name 집합 필드 이름.
   * @param count 원소 개수(직렬화 시 알고 있는 크기의 힌트).
   * @return 실제로 순회할 원소 수.
   */
  virtual std::size_t OnSetBegin(const Field& name,
                                 std::size_t count) = 0;  // 반환=원소 수
  /** @brief 집합 종료를 알린다. */
  virtual void OnSetEnd() = 0;

  /**
   * @brief 방향(직렬화/역직렬화)을 판별한다.
   * @note map의 "생성+insert vs 순회" 비대칭 처리에만 사용된다.
   * @return Serializer는 false, Deserializer는 true.
   */
  [[nodiscard]] virtual bool IsReading() const = 0;

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

  /** @brief OnVariantBegin이 "판별자 없음 — trial을 쓰라"고 알리는 sentinel. */
  static constexpr std::size_t kVariantUseTrial =
      std::numeric_limits<std::size_t>::max();

  /**
   * @brief variant 진입 — 활성 대안을 판별한다.
   *
   * 자기서술 포맷(값에 타입 태그가 있어 trial로 대안을 가릴 수 있는 경우)은
   * 이 훅을 구현하지 않고 기본값 kVariantUseTrial을 반환한다 -> 베이스가
   * 기존 trial 경로를 쓴다(무변경). 타입 소거 포맷(protobuf식)은 이 훅을
   * 구현한다:
   *  - 쓰기: active_index를 판별자로 방출하고 (반환값은 무시된다).
   *  - 읽기: 판별자를 읽어 활성 대안의 인덱스를 반환한다 -> 베이스가 trial
   *    없이 그 대안만 방문한다.
   * @param name 필드.
   * @param alt_count 대안 개수(범위 검증용).
   * @param active_index 쓰기 시 활성 대안 인덱스(읽기 시 무의미).
   * @return 읽기: 활성 인덱스 또는 kVariantUseTrial. 쓰기: 무시됨.
   */
  virtual std::size_t OnVariantBegin(const Field& name, std::size_t alt_count,
                                     std::size_t active_index) {
    static_cast<void>(name);
    static_cast<void>(alt_count);
    static_cast<void>(active_index);
    return kVariantUseTrial;
  }
  /** @brief variant 종료(판별자 기반 포맷의 프레임 정리용). */
  virtual void OnVariantEnd() {}

  // ── 스칼라 리프 (포맷/방향별 구현) ──
  /**
   * @brief 문자열 필드 이름을 명시적으로 Field로 감싸 기존 호출 문법을
   * 지원한다.
   */
  template <typename T>
  Visitor& Visit(std::string_view name, T& value) {
    return Visit(Field{name}, value);
  }

  // clang-format off
  /** @brief bool 값을 방문한다. */
  virtual Visitor& Visit(const Field& name, bool& value) = 0;
  /** @brief byte 값을 방문한다. */
  virtual Visitor& Visit(const Field& name, std::byte& value) = 0;
  /** @brief int8_t 값을 방문한다. */
  virtual Visitor& Visit(const Field& name, std::int8_t& value) = 0;
  /** @brief uint8_t 값을 방문한다. */
  virtual Visitor& Visit(const Field& name, std::uint8_t& value) = 0;
  /** @brief int16_t 값을 방문한다. */
  virtual Visitor& Visit(const Field& name, std::int16_t& value) = 0;
  /** @brief uint16_t 값을 방문한다. */
  virtual Visitor& Visit(const Field& name, std::uint16_t& value) = 0;
  /** @brief int32_t 값을 방문한다. */
  virtual Visitor& Visit(const Field& name, std::int32_t& value) = 0;
  /** @brief uint32_t 값을 방문한다. */
  virtual Visitor& Visit(const Field& name, std::uint32_t& value) = 0;
  /** @brief int64_t 값을 방문한다. */
  virtual Visitor& Visit(const Field& name, std::int64_t& value) = 0;
  /** @brief uint64_t 값을 방문한다. */
  virtual Visitor& Visit(const Field& name, std::uint64_t& value) = 0;
  /** @brief float 값을 방문한다. */
  virtual Visitor& Visit(const Field& name, float& value) = 0;
  /** @brief double 값을 방문한다. */
  virtual Visitor& Visit(const Field& name, double& value) = 0;

  /** @brief DLL이 소유한 바이트열 view를 주고받는다. */
  virtual Visitor& Visit(const Field& name,
                         std::span<const std::byte>& value) = 0;

  /** @brief DLL이 소유한 문자열 view를 주고받는다. */
  virtual Visitor& Visit(const Field& name, std::string_view& value) = 0;
  // clang-format on

  Visitor& Visit(const Field& name, std::string& value) {
    std::string_view view =
        IsReading() ? std::string_view{} : std::string_view(value);
    Visit(name, view);
    if (IsReading()) {
      value.assign(view.data(), view.size());
    }
    return *this;
  }
  Visitor& Visit(const Field& name, std::vector<std::byte>& value) {
    std::span<const std::byte> view =
        IsReading() ? std::span<const std::byte>{}
                    : std::span<const std::byte>(value.data(), value.size());
    Visit(name, view);
    if (IsReading()) {
      value.assign(view.begin(), view.end());
    }
    return *this;
  }

  // ── 제네릭 컨테이너/중첩 (베이스에 1번, 전 포맷·양방향 공용) ──

  /**
   * @brief vector류(vector/deque) 컨테이너를 방문한다.
   *
   * resize 후 제자리에서 원소를 방문하므로 IsReading 분기 없이 양방향
   * 공용으로 동작한다.
   *  - 직렬화: n=현재 크기, resize는 no-op, c[i]를 기록.
   *  - 역직렬화: n=파싱된 개수, resize로 성장/축소 후 c[i]를 채움.
   * @param name 필드 이름.
   * @param container 방문할 컨테이너(SeqLike).
   * @return *this.
   * @note vector<std::byte>는 전용 가상 Visit 오버로드가 우선한다.
   */
  template <SeqLike C>
  Visitor& Visit(const Field& name, C& container) {
    std::size_t element_count = OnSeqBegin(name, container.size());
    if (element_count == std::numeric_limits<std::size_t>::max()) {
      return *this;
    }
    container.resize(element_count);
    for (std::size_t alternative_index = 0; alternative_index < element_count;
         ++alternative_index) {
      Visit(std::string_view{},
            container[alternative_index]);  // 재귀 — 원소는 이름 없음
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
   * @param mapping 방문할 컨테이너(MapLike).
   * @return *this.
   */
  template <MapLike C>
  Visitor& Visit(const Field& name, C& mapping) {
    // multimap 판정을 맵 진입 전에 알린다(진입 시 검증 완화에 사용).
    if constexpr (MultiMapLike<C>) {
      OnDuplicateKeysBegin();
    }
    std::size_t element_count = OnMapBegin(name, mapping.size());
    if (element_count == std::numeric_limits<std::size_t>::max()) {
      return *this;  // 실패/미구현 sentinel (seq와 동일) -> 무한 루프 방지
    }
    if (IsReading()) {
      mapping.clear();
      for (std::size_t alternative_index = 0; alternative_index < element_count;
           ++alternative_index) {
        typename C::key_type key{};
        typename C::mapped_type value{};
        Visit(std::string_view{}, key);    // 상태: 키 처리 -> 값으로 전이
        Visit(std::string_view{}, value);  // 상태: 값 처리 -> 키로 전이
        mapping.insert({std::move(key), std::move(value)});  // multimap도 OK
      }
    } else {
      for (auto& [map_key, mapped_value] : mapping) {
        typename C::key_type mutable_key =
            map_key;  // 맵의 키는 const -> 방문 위해 복사
        Visit(std::string_view{}, mutable_key);
        Visit(std::string_view{}, mapped_value);
      }
    }
    OnMapEnd();
    return *this;
  }

  /**
   * @brief set류(set/unordered_set/multiset 등) 컨테이너를 방문한다.
   *
   * 원소는 불변(키)이므로 시퀀스처럼 제자리 방문이 불가능하다 —
   * 읽기는 "기본 생성 -> 방문 -> insert", 쓰기는 원소를 복사해 방문한다.
   * @param name 필드 이름.
   * @param container 방문할 컨테이너(SetLike).
   * @return *this.
   */
  template <SetLike C>
  Visitor& Visit(const Field& name, C& container) {
    std::size_t element_count = OnSetBegin(name, container.size());
    if (element_count == std::numeric_limits<std::size_t>::max()) {
      return *this;  // 실패/미구현 sentinel -> 무한 루프 방지
    }
    if (IsReading()) {
      container.clear();
      for (std::size_t alternative_index = 0; alternative_index < element_count;
           ++alternative_index) {
        typename C::value_type key{};
        Visit(std::string_view{}, key);
        container.insert(std::move(key));  // multiset도 OK
      }
    } else {
      for (const auto& element : container) {
        typename C::value_type copy =
            element;  // set 원소는 const -> 방문 위해 복사
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
   * @param value 방문할 std::variant.
   * @return *this.
   */
  template <typename... Ts>
  Visitor& Visit(const Field& name, std::variant<Ts...>& value) {
    if (IsReading()) {
      const std::size_t active_index =
          OnVariantBegin(name, sizeof...(Ts), value.index());
      if (active_index == kVariantUseTrial) {
        // 자기서술 포맷: 대안을 순서대로 시도(첫 성공에서 멈춤).
        (void)(TryAlternative<Ts>(name, value) || ...);
      } else {
        // 타입 소거 포맷: 판별된 대안만 방문.
        VisitByIndex<Ts...>(name, value, active_index);
      }
      OnVariantEnd();
    } else {
      OnVariantBegin(name, sizeof...(Ts), value.index());  // 쓰기: 판별자 방출
      std::visit([&](auto& alt) { Visit(name, alt); }, value);
      OnVariantEnd();
    }
    return *this;
  }

  /**
   * @brief 중첩 Schema(구조체)를 방문한다.
   *
   * OnObjectBegin -> Schema::Accept -> OnObjectEnd 순으로 호출한다.
   * 정의는 visitor.cc 참고.
   * @param name 필드 이름.
   * @param value 방문할 중첩 Schema.
   * @return *this.
   */
  Visitor& Visit(const Field& name, Schema& value);

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
   * @param value 대상 std::variant.
   * @return 이 대안이 채택되었으면 true.
   */
  template <typename T, typename... Ts>
  bool TryAlternative(const Field& name, std::variant<Ts...>& value) {
    OnTrialBegin();
    Visit(name, value.template emplace<T>());
    return OnTrialCommit();
  }

  /**
   * @brief 인덱스로 지정된 대안만 emplace 후 방문한다(판별자 기반 경로).
   * @tparam Ts variant의 전체 대안 타입 목록.
   * @param name 필드.
   * @param value 대상 std::variant.
   * @param active_index 방문할 대안 인덱스(범위 밖이면 아무것도 하지 않음).
   */
  template <typename... Ts>
  void VisitByIndex(const Field& name, std::variant<Ts...>& value,
                    std::size_t active_index) {
    std::size_t alternative_index = 0;
    // i가 idx와 같은 대안에서만 emplace+Visit(&& 단축평가), 이후 || 로 멈춘다.
    (void)(((alternative_index++ == active_index) &&
            (Visit(name, value.template emplace<Ts>()), true)) ||
           ...);
  }

  [[nodiscard]] std::uint16_t MachineId() const { return machine_id_; }
  [[nodiscard]] std::size_t IndentSize() const { return indent_size_; }

 private:
  /** @brief Snowflake ID 생성에 쓰이는 머신 식별자. */
  std::uint16_t machine_id_;

  /** @brief 포맷 출력 시 사용하는 들여쓰기 폭(공백 수). */
  std::size_t indent_size_ = 2;
};

}  // namespace bedrock::archive
