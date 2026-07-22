/**
 * @file transciber.h
 * @brief 포맷 독립적인 직렬화/역직렬화 공통 타입과 베이스 클래스.
 *
 * 스펙(YAML 3.1)의 파이프라인에서 포맷 독립인 두 프로세스를 베이스가
 * 구현한다:
 *  - Construct(Deserializer): 표현 트리(Node) 위를 이름/순서로 탐색하며
 *    목표 C++ 타입으로 값을 해소한다.
 *  - Represent(Serializer): Schema 방문 결과를 표현 트리로 구성한다.
 * 두 방향은 같은 골격 — 프레임 스택 + 컨텍스트(필드/원소/맵 키·값
 * 교대) — 의 역연산이다(읽기=소비, 쓰기=생산).
 *
 * 포맷별 파생 클래스는 텍스트↔트리 훅만 구현한다:
 *  - LoadDocument(스트림→Node)·IsBinaryScalar(바이너리 표기 판정)
 *  - PresentDocument(Node→스트림)
 * 교환 자료구조(Node, ValueType)는 transcriber/node.h에 있다.
 */
#pragma once
#include "common/archive/transcriber/node.h"
#include "common/archive/visitor.h"
#include "common/data_types/snowflake.h"
#include "common/data_types/status.h"

namespace bedrock::archive::transcriber {

/** @brief 트랜스크라이버(직렬화/역직렬화) 전용 오류 코드. */
enum class TranscriberError : std::uint32_t {
  /** @brief 성공. */
  kSuccess = 0,
  /** @brief 대상이 존재하지 않음(No such entry). */
  kNoENT = 1,
  /** @brief 스트림이 null이거나 유효하지 않음. */
  kNullStream = 2,
  /** @brief 데이터가 손상되었거나 형식이 어긋남. */
  kCorrupted = 3,
  /** @brief 그 외 일반 오류. */
  kError = 0xFFFFFFFF,
};

/**
 * @brief TranscriberError 전용 std::error_category를 반환한다.
 * @return 국제화(i18n)된 메시지를 제공하는 정적 카테고리 인스턴스.
 */
const std::error_category& TranscriberCategory() noexcept;
/**
 * @brief TranscriberError를 std::error_code로 변환한다.
 * @param e 변환할 오류 값.
 * @return TranscriberCategory()에 연결된 std::error_code.
 */
std::error_code make_error_code(TranscriberError e) noexcept;

/**
 * @brief 포맷 독립적인 역직렬화기 베이스 클래스(읽기 방향, Construct).
 *
 * 첫 방문 때 LoadDocument로 입력 스트림을 표현 트리로 적재하고, 이후의
 * 모든 구조 훅/Visit은 트리 위를 프레임 스택으로 탐색한다. 필드는
 * 이름으로 조회하므로 파일의 필드 순서와 무관하고 모르는 키는
 * 무시된다. std::variant는 trial 훅으로 대안을 시도하고 실패 시 순회
 * 상태를 되감는다.
 */
class Deserializer : public Visitor {
 public:
  /**
   * @brief 역직렬화기를 생성한다(적재는 첫 방문 때 지연 수행).
   * @param machine_id Snowflake ID 생성에 쓰이는 머신 식별자.
   * @param input_stream 읽어 들일 입력 스트림.
   */
  Deserializer(std::uint16_t machine_id, std::istream& input_stream)
      : Visitor(machine_id),
        status(make_error_code(TranscriberError::kSuccess)),
        _input_stream(input_stream) {}
  /** @brief 가상 소멸자. */
  virtual ~Deserializer() override;

  /**
   * @brief 방향 판별: 역직렬화기는 항상 읽기 방향이다.
   * @return 항상 true.
   */
  bool IsReading() const final override { return true; }

  /** @brief 루트 진입 — 지연 적재 후 문서 루트(또는 name 항목)로 이동. */
  void OnRootBegin(std::string_view name) final override;
  /** @brief 루트 종료 — 순회 프레임을 닫는다. */
  void OnRootEnd() final override;
  /** @brief 객체 진입 — 현재 컨텍스트에서 매핑 노드를 찾아 내려간다. */
  void OnObjectBegin(std::string_view name) final override;
  /** @brief 객체 종료 — 순회 프레임을 닫는다. */
  void OnObjectEnd() final override;
  /**
   * @brief 시퀀스 진입 — 파싱된 원소 수를 반환한다(호출자가 resize).
   * @return 원소 수. 노드가 없거나 시퀀스가 아니면 실패 센티널.
   */
  std::size_t OnSeqBegin(std::string_view name,
                         std::size_t count) final override;
  /** @brief 시퀀스 종료 — 순회 프레임을 닫는다. */
  void OnSeqEnd() final override;
  /**
   * @brief 맵 진입 — 파싱된 엔트리 수를 반환한다(키→값→키→값 순회).
   * @return 엔트리 수. 노드가 없거나 매핑이 아니면 실패 센티널.
   */
  std::size_t OnMapBegin(std::string_view name,
                         std::size_t count) final override;
  /** @brief 맵 종료 — 순회 프레임을 닫는다. */
  void OnMapEnd() final override;
  /**
   * @brief 집합 진입 — 파싱된 원소 수를 반환한다.
   * 집합 표기가 없는 포맷은 시퀀스로 제시하므로 kSet/kSequence 노드를
   * 모두 받아들인다.
   * @return 원소 수. 노드가 없거나 집합/시퀀스가 아니면 실패 센티널.
   */
  std::size_t OnSetBegin(std::string_view name,
                         std::size_t count) final override;
  /** @brief 집합 종료 — 순회 프레임을 닫는다. */
  void OnSetEnd() final override;

  /** @brief variant trial 시작 — 순회 상태/상태코드를 저장한다. */
  void OnTrialBegin() final override;
  /**
   * @brief variant trial 확정 — 대안이 실패했으면 저장 지점으로 되감고
   * false를 반환해 다음 대안을 시도하게 한다.
   */
  bool OnTrialCommit() final override;

  // clang-format off
  /** @brief bool 값을 읽는다("true"/"false"). */
  Deserializer& Visit(std::string_view name, bool& value) final override;
  /** @brief byte 값을 읽는다(16진 2자리). */
  Deserializer& Visit(std::string_view name, std::byte& value) final override;
  /** @brief int8_t 값을 읽는다. */
  Deserializer& Visit(std::string_view name, std::int8_t& value) final override;
  /** @brief uint8_t 값을 읽는다. */
  Deserializer& Visit(std::string_view name, std::uint8_t& value) final override;
  /** @brief int16_t 값을 읽는다. */
  Deserializer& Visit(std::string_view name, std::int16_t& value) final override;
  /** @brief uint16_t 값을 읽는다. */
  Deserializer& Visit(std::string_view name, std::uint16_t& value) final override;
  /** @brief int32_t 값을 읽는다. */
  Deserializer& Visit(std::string_view name, std::int32_t& value) final override;
  /** @brief uint32_t 값을 읽는다. */
  Deserializer& Visit(std::string_view name, std::uint32_t& value) final override;
  /** @brief int64_t 값을 읽는다. */
  Deserializer& Visit(std::string_view name, std::int64_t& value) final override;
  /** @brief uint64_t 값을 읽는다. */
  Deserializer& Visit(std::string_view name, std::uint64_t& value) final override;
  /** @brief float 값을 읽는다. */
  Deserializer& Visit(std::string_view name, float& value) final override;
  /** @brief double 값을 읽는다. */
  Deserializer& Visit(std::string_view name, double& value) final override;

  /** @brief 바이트열 값을 읽는다(바이너리 표기 필수, base64 디코드). */
  Deserializer& Visit(std::string_view name, std::vector<std::byte>& value) final override;

  /** @brief 문자열 값을 읽는다(디코드된 스칼라 그대로). */
  Deserializer& Visit(std::string_view name, std::string& value) final override;
  // clang-format on

  /** @brief 현재까지의 역직렬화 상태(성공/실패). */
  Status status;

 protected:
  // ── 포맷 훅 (파생 클래스가 구현하는 전부) ──
  /**
   * @brief 입력 스트림을 표현 트리로 적재한다(Parse+Compose).
   * 문법/조립 오류는 위치 정보를 담은 실패 Status로 보고한다.
   * @param in 입력 스트림.
   * @param out 성공 시 문서 루트 노드가 채워진다.
   */
  virtual Status LoadDocument(std::istream& in, Node& out) = 0;
  /**
   * @brief 이 스칼라가 포맷상 바이너리 표기인지 판정한다
   * (예: YAML의 !!binary 태그). variant(문자열|바이트열) trial의 판별
   * 근거가 된다.
   */
  virtual bool IsBinaryScalar(const Node& n) const = 0;

  /**
   * @brief 스트림이 seek 가능한지 검사한다(추정).
   * @param _input_stream 검사할 입력 스트림.
   * @note 선언만 있고 정의부가 없다 — 아직 구현되지 않았다(TBD).
   */
  bool IsSeekable(std::istream& _input_stream);

  /** @brief 역직렬화 대상 입력 스트림. */
  std::istream& _input_stream;

 private:
  /** @brief 순회 프레임 하나 — 현재 노드와 소비 진행 상태. */
  struct Frame {
    /** @brief 현재 노드(nullptr = 상위 실패로 인한 더미 프레임). */
    const Node* node;
    /** @brief kItem/kMap의 진행 인덱스. */
    std::size_t idx;
    /** @brief 프레임의 순회 방식(쓰기 쪽 BuildFrame::Ctx와 대칭). */
    enum class Ctx : std::uint8_t {
      /** @brief 객체 필드 — 이름으로 조회. */
      kFields,
      /** @brief 시퀀스/집합 — 인덱스 순서 소비. */
      kItem,
      /** @brief 맵 — 키→값 교대 소비. */
      kMap,
    } ctx;
    /** @brief kMap: 다음 방문이 값 차례인지. */
    bool value_turn;
  };
  /** @brief variant trial의 복원 지점(순회 프레임 + 상태코드). */
  struct Savepoint {
    std::vector<Frame> frames;
    Status status;
  };

  /** @brief 첫 방문 때 LoadDocument를 수행한다(오류는 status로). */
  void EnsureLoaded();
  /**
   * @brief 현재 프레임에서 다음 방문 대상 노드를 찾는다.
   * kFields는 이름 조회(없으면 kNoENT), kItem/kMap은 순서 소비.
   * @return 대상 노드. 실패 시 nullptr(status에 사유).
   */
  const Node* ResolveChild(std::string_view name);

  /** @brief 적재가 끝난 첫 문서의 표현 트리. */
  Node _doc;
  /** @brief EnsureLoaded 수행 여부. */
  bool _loaded = false;
  /** @brief 순회 프레임 스택. */
  std::vector<Frame> _frames;
  /** @brief variant trial 복원 지점 스택. */
  std::vector<Savepoint> _savepoints;
};

/**
 * @brief 포맷 독립적인 직렬화기 베이스 클래스(쓰기 방향, Represent).
 *
 * Schema 방문 결과를 표현 트리(Node)로 구성하고, Flush()에서 포맷별
 * PresentDocument가 트리를 실제 출력 스트림에 기록한다. 트리 구성은
 * 읽기 쪽과 대칭인 프레임 스택으로 추적한다(읽기=소비, 쓰기=생산).
 */
class Serializer : public Visitor {
 public:
  /**
   * @brief 직렬화기를 생성한다.
   * @param machine_id Snowflake ID 생성에 쓰이는 머신 식별자.
   * @param output_stream 기록할 출력 스트림.
   */
  Serializer(std::uint16_t machine_id, std::ostream& output_stream)
      : Visitor(machine_id),
        _output_stream(output_stream),
        _status(make_error_code(TranscriberError::kSuccess)) {}
  /** @brief 가상 소멸자. */
  virtual ~Serializer() override;

  /**
   * @brief 방향 판별: 직렬화기는 항상 쓰기 방향이다.
   * @return 항상 false.
   */
  bool IsReading() const final override { return false; }

  /**
   * @brief 구성된 표현 트리를 PresentDocument로 출력 스트림에 기록한다.
   * 한 번 쓰면 트리는 소비된다(중복 Flush는 중복 출력하지 않음).
   * @note 파생 클래스의 소멸자가 호출한다(베이스 소멸자에서 부르면
   * 파생 PresentDocument가 이미 소멸해 있으므로 금지).
   * @return 기록 결과 상태(트리가 비었거나 스트림이 비정상이면 실패).
   */
  Status Flush();

  /** @brief 루트 매핑 구성을 시작한다. */
  void OnRootBegin(std::string_view name) final override;
  /** @brief 루트를 확정한다(name이 있으면 {name: …}로 감싼다). */
  void OnRootEnd() final override;
  /** @brief 매핑 노드 구성을 시작한다(자식 = 필드). */
  void OnObjectBegin(std::string_view name) final override;
  /** @brief 매핑 노드를 완성해 부모에 붙인다. */
  void OnObjectEnd() final override;
  /**
   * @brief 시퀀스 노드 구성을 시작한다(자식 = 원소).
   * @return 이미 실패 상태면 실패 센티널, 아니면 count 그대로.
   */
  std::size_t OnSeqBegin(std::string_view name,
                         std::size_t count) final override;
  /** @brief 시퀀스 노드를 완성해 부모에 붙인다. */
  void OnSeqEnd() final override;
  /**
   * @brief 맵 노드 구성을 시작한다(자식 = 키→값 교대).
   * @return 이미 실패 상태면 실패 센티널, 아니면 count 그대로.
   */
  std::size_t OnMapBegin(std::string_view name,
                         std::size_t count) final override;
  /** @brief 맵 노드를 완성해 부모에 붙인다. */
  void OnMapEnd() final override;
  /**
   * @brief 집합 노드 구성을 시작한다(자식 = 원소).
   * @return 이미 실패 상태면 실패 센티널, 아니면 count 그대로.
   */
  std::size_t OnSetBegin(std::string_view name,
                         std::size_t count) final override;
  /** @brief 집합 노드를 완성해 부모에 붙인다. */
  void OnSetEnd() final override;

  // clang-format off
  /** @brief bool 값을 표현 트리 스칼라로 방문한다. */
  Serializer& Visit(std::string_view name, bool& value) final override;
  /** @brief byte 값을 표현 트리 스칼라로 방문한다. */
  Serializer& Visit(std::string_view name, std::byte& value) final override;
  /** @brief int8_t 값을 표현 트리 스칼라로 방문한다. */
  Serializer& Visit(std::string_view name, std::int8_t& value) final override;
  /** @brief uint8_t 값을 표현 트리 스칼라로 방문한다. */
  Serializer& Visit(std::string_view name, std::uint8_t& value) final override;
  /** @brief int16_t 값을 표현 트리 스칼라로 방문한다. */
  Serializer& Visit(std::string_view name, std::int16_t& value) final override;
  /** @brief uint16_t 값을 표현 트리 스칼라로 방문한다. */
  Serializer& Visit(std::string_view name, std::uint16_t& value) final override;
  /** @brief int32_t 값을 표현 트리 스칼라로 방문한다. */
  Serializer& Visit(std::string_view name, std::int32_t& value) final override;
  /** @brief uint32_t 값을 표현 트리 스칼라로 방문한다. */
  Serializer& Visit(std::string_view name, std::uint32_t& value) final override;
  /** @brief int64_t 값을 표현 트리 스칼라로 방문한다. */
  Serializer& Visit(std::string_view name, std::int64_t& value) final override;
  /** @brief uint64_t 값을 표현 트리 스칼라로 방문한다. */
  Serializer& Visit(std::string_view name, std::uint64_t& value) final override;
  /** @brief float 값을 표현 트리 스칼라로 방문한다. */
  Serializer& Visit(std::string_view name, float& value) final override;
  /** @brief double 값을 표현 트리 스칼라로 방문한다. */
  Serializer& Visit(std::string_view name, double& value) final override;

  /** @brief 바이트열(blob) 값을 바이너리 스칼라(base64)로 방문한다. */
  Serializer& Visit(std::string_view name, std::vector<std::byte>& value) final override;

  /** @brief 문자열 값을 표현 트리 스칼라로 방문한다. */
  Serializer& Visit(std::string_view name, std::string& value) final override;
  // clang-format on

 protected:
  // ── 포맷 훅 (파생 클래스가 구현하는 전부) ──
  /**
   * @brief 표현 트리를 텍스트로 렌더링해 _output_stream에 쓴다(Present).
   * 인용/이스케이프/들여쓰기 등 제시 규칙은 전부 이 구현에 속한다.
   * @param root 문서 루트 노드.
   */
  virtual Status PresentDocument(const Node& root) = 0;

  /**
   * @brief 스트림이 seek 가능한지 검사한다(추정).
   * @param _output_stream 검사할 출력 스트림.
   * @note 선언만 있고 정의부가 없다 — 아직 구현되지 않았다(TBD).
   */
  bool IsSeekable(std::ostream& _output_stream);

  /** @brief 직렬화 결과를 기록할 출력 스트림. */
  std::ostream& _output_stream;

  /** @brief 현재까지의 직렬화 상태(성공/실패). */
  Status _status;

 private:
  /** @brief 구성 중 컨테이너 하나 — 완성 시 부모에 붙일 키를 함께 든다. */
  struct BuildFrame {
    /** @brief 구성 중인 컨테이너 노드. */
    Node node;
    /** @brief keyed=true일 때 부모 매핑에서의 키 노드. */
    Node key;
    /** @brief kMap: 값과 짝지을 때까지 보관하는 키 노드. */
    Node pending_key;
    /** @brief 부모가 매핑이라 키가 필요한지(원소 컨텍스트면 false). */
    bool keyed = false;
    /** @brief 자식들의 붙이기 방식(읽기 쪽 Frame::Ctx와 대칭). */
    enum class Ctx : std::uint8_t {
      /** @brief 객체 필드 — {이름: 값} 쌍. */
      kFields,
      /** @brief 시퀀스/집합 — 원소로 추가. */
      kItem,
      /** @brief 맵 — 키→값 교대. */
      kMap,
    } ctx = Ctx::kFields;
    /** @brief kMap: 다음 방문이 값 차례인지. */
    bool value_turn = false;
  };

  /** @brief 스칼라 노드를 만든다(vtype = 쓰기 힌트). */
  static Node MakeScalarNode(std::string str, ValueType type);
  /** @brief plain으로 그대로 출력되는 키 노드(필드 이름용)를 만든다. */
  static Node MakePlainKey(std::string_view name);
  /** @brief 완성된 노드를 최상단 컨테이너에 붙인다(매핑이면 키와 짝). */
  void AttachToTop(Node&& key, bool keyed, Node&& n);
  /**
   * @brief 현재 프레임 컨텍스트로 새 항목의 키를 결정한다.
   * 필드=이름, 원소=키 없음, 맵 값=캡처된 키. 맵 키 위치의 컨테이너
   * (복합 키)는 미지원 — 실패 상태를 남긴다.
   */
  bool ResolveEntryKey(std::string_view name, Node& key, bool& keyed);
  /** @brief 컨테이너 구성 시작 공통 처리(키 결정→프레임 push). */
  void BeginContainer(std::string_view name, Node::Kind kind,
                      BuildFrame::Ctx child_ctx);
  /** @brief 컨테이너 구성 종료 공통 처리(프레임 pop→부모에 붙이기). */
  void EndContainer();
  /** @brief 렌더된 스칼라를 현재 프레임 컨텍스트에 따라 붙인다. */
  Status EmitScalar(std::string_view name, std::string value_str,
                    ValueType type);

  /** @brief 구성 중 컨테이너 스택(완성 시 부모로 이동). */
  std::vector<BuildFrame> _building;
  /** @brief 구성이 끝난 문서 루트(Flush가 소비). */
  Node _root;
  /** @brief 루트 구성 완료 여부(Flush 1회 소비 후 해제). */
  bool _root_done = false;
  /** @brief OnRootBegin의 이름(비어 있지 않으면 {name: …}로 감싼다). */
  std::string _root_name;
};

}  // namespace bedrock::archive::transcriber

/**
 * @brief TranscriberError를 std::error_code 소스로 등록한다.
 *
 * 이 특수화로 TranscriberError 값을 std::error_code/error_condition
 * 생성자에 그대로 전달할 수 있게 된다.
 */
template <>
struct std::is_error_code_enum<bedrock::archive::transcriber::TranscriberError>
    : std::true_type {};
