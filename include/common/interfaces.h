/**
 * @file interfaces.h
 * @brief bedrock 공용 인터페이스와 CRTP 헬퍼 정의.
 */
#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include <optional>

#include "types_enums.h"

namespace bedrock {

/**
 * @brief 유효성 검사가 가능한 객체를 위한 인터페이스.
 */
class Validatable {
 public:
  /** @brief 기본 생성자. */
  Validatable() = default;
  /** @brief 복사 생성자. */
  Validatable(const Validatable&) = default;
  /** @brief 복사 대입 연산자. */
  Validatable& operator=(const Validatable&) = default;
  /** @brief 가상 소멸자. */
  virtual ~Validatable();

  /**
   * @brief 객체가 유효한 상태인지 확인한다.
   * @return 유효하면 true.
   */
  virtual bool IsValid() const = 0;
};

/**
 * @brief 읽기/쓰기가 가능한 객체를 위한 인터페이스.
 * @tparam StatusType StatusEnum을 만족하는 상태 열거형 타입.
 */
template <typename StatusType>
  requires StatusEnum<StatusType>
class ReadWritable {
 public:
  /** @brief 기본 생성자. */
  ReadWritable() = default;
  /** @brief 복사 생성자. */
  ReadWritable(const ReadWritable&) = default;
  /** @brief 복사 대입 연산자. */
  ReadWritable& operator=(const ReadWritable&) = default;

  /** @brief 가상 소멸자. */
  virtual ~ReadWritable() = default;

  /**
   * @brief 데이터를 읽는다.
   * @param request_size 요청할 데이터 크기.
   * @return 읽은 데이터와 실제 크기, 상태를 담은 결과.
   */
  virtual DataWithStatus<std::pair<std::vector<std::byte>, std::uint32_t>,
                         StatusType>
  Read(std::uint32_t request_size) = 0;
  /**
   * @brief 데이터를 쓴다.
   * @param data 기록할 데이터.
   * @return 처리 결과 상태.
   */
  virtual StatusType Write(std::span<const std::byte> data) = 0;
};

/**
 * @brief 정적 구현 훅(hook)을 통해 실패할 수 있는 생성을 지원하는 CRTP
 * 헬퍼.
 *
 * 파생 클래스는 다음을 만족해야 한다:
 * 1) 비공개(non-public) 정적 함수 `CreateImpl(...)`에 접근할 수 있도록
 *    `ConstructFailable<Derived>`를 friend로 선언한다.
 * 2) `std::optional<Derived>`(또는 호환 타입)를 반환하는 정적 함수
 *    `CreateImpl(Args...)`를 제공한다.
 * 3) 직접 생성을 막고 `Create(...)`를 통해서만 생성하도록 생성자를
 *    비공개(private/protected)로 만든다.
 * @tparam ClassType 실패 가능한 생성을 지원할 파생 클래스 타입.
 */
template <typename ClassType>
class ConstructFailable {
 public:
  /**
   * @brief CreateImpl(...)을 호출해 ClassType을 생성한다.
   * @tparam Args CreateImpl에 전달할 인자 타입들.
   * @param args CreateImpl에 전달할 인자들.
   * @return 생성에 성공하면 값이 있는 std::optional<ClassType>.
   */
  template <typename... Args>
  static std::optional<ClassType> Create(Args&&... args) {
    return ClassType::CreateImpl(std::forward<Args>(args)...);
  }
};

}  // namespace bedrock
