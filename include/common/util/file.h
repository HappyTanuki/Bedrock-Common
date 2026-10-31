/**
 * @file file.h
 * @brief 파일 읽기/쓰기 및 파일 내 패턴 검색 유틸리티.
 */
#pragma once
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <string_view>

#include "common/data_types/status.h"

namespace bedrock::util {

/** @brief FindFirstAppearanceFromFile 의 실패 반환값(열기 실패/거대 패턴). */
inline constexpr std::uint64_t kFileSearchFailed =
    std::numeric_limits<std::uint64_t>::max();

/**
 * @brief 파일 전체 내용을 Common 내부에서 소유하고 뷰로 빌려준다.
 *
 * @details Bytes()와 Text()가 반환하는 뷰는 Common이 소유하는 저장소를 빌린다.
 * Read() 성공, 이동 생성 또는 이동 대입의 원본 또는 대상, 객체 소멸 시
 * 기존 뷰는 무효화된다. 해당 작업 후 뷰를 다시 얻어야 하며, 뷰가 가리키는
 * 데이터를 소유해야 하면 별도로 복사해야 한다. Read() 실패 시 기존 내용과
 * 뷰는 유효하다. 이동된 객체의 Bytes()와 Text()는 빈 뷰를 반환하며 이후
 * Read()할 수 있다.
 */
class FileContents {
 public:
  /** @brief 빈 파일 내용을 생성한다. */
  FileContents();
  /** @brief 내부 파일 내용을 해제한다. */
  ~FileContents();
  /** @brief 복사 생성을 금지한다. */
  FileContents(const FileContents&) = delete;
  /** @brief 복사 대입을 금지한다. */
  FileContents& operator=(const FileContents&) = delete;
  /** @brief 내부 파일 내용을 이동한다. */
  FileContents(FileContents&&) noexcept;
  /** @brief 기존 내용을 해제하고 파일 내용을 이동한다. */
  FileContents& operator=(FileContents&&) noexcept;

  /**
   * @brief 파일 전체를 읽어 내부에 저장한다.
   * @param path 읽을 파일 경로.
   * @return 성공 시 성공 상태, 열기 실패 시 kNoENT, 읽기 실패 시 kError.
   */
  [[nodiscard]] bedrock::Status Read(const std::filesystem::path& path) &;
  /**
   * @brief 내부 파일 내용을 바이트 뷰로 반환한다.
   * @return 내부 바이트를 가리키는 뷰.
   */
  [[nodiscard]] std::span<const std::uint8_t> Bytes() const noexcept;
  /**
   * @brief 내부 파일 내용을 문자열 뷰로 반환한다.
   * @return 내부 바이트를 문자로 해석한 뷰.
   */
  [[nodiscard]] std::string_view Text() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
/**
 * @brief 바이트 데이터를 파일에 쓴다.
 * @param path 쓸 파일 경로.
 * @param data 기록할 바이트 데이터.
 * @return 성공하면 true, 파일을 열 수 없으면 false.
 */
bool WriteToFile(const std::filesystem::path& path,
                 std::span<const std::uint8_t> data);
/**
 * @brief 문자열 데이터를 파일에 쓴다.
 * @param path 쓸 파일 경로.
 * @param data 기록할 문자열 데이터.
 * @return 성공하면 true, 파일을 열 수 없으면 false.
 */
bool WriteToFile(const std::filesystem::path& path, std::string_view data);

/**
 * @brief 파일에서 패턴이 처음 나타나는 위치를 찾는다.
 * @param path 검색할 파일 경로.
 * @param pattern 찾을 패턴(문자열).
 * @return 찾으면 파일 내 바이트 오프셋, 못 찾으면 파일 크기,
 *         파일을 열 수 없거나 pattern이 내부 청크 크기보다 크면
 *         kFileSearchFailed.
 */
std::uint64_t FindFirstAppearanceFromFile(const std::filesystem::path& path,
                                          std::string_view pattern);
/**
 * @brief 파일에서 패턴이 처음 나타나는 위치를 찾는다.
 * @param path 검색할 파일 경로.
 * @param pattern 찾을 패턴(바이트 시퀀스).
 * @return 찾으면 파일 내 바이트 오프셋, 못 찾으면 파일 크기,
 *         파일을 열 수 없거나 pattern이 내부 청크 크기보다 크면
 *         kFileSearchFailed.
 */
std::uint64_t FindFirstAppearanceFromFile(
    const std::filesystem::path& path, std::span<const std::uint8_t> pattern);

}  // namespace bedrock::util
