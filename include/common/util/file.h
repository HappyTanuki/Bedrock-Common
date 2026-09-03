/**
 * @file file.h
 * @brief 파일 읽기/쓰기 및 파일 내 패턴 검색 유틸리티.
 */
#pragma once
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bedrock::util {

/** @brief FindFirstAppearanceFromFile 의 실패 반환값(열기 실패/거대 패턴). */
inline constexpr std::uint64_t kFileSearchFailed =
    std::numeric_limits<std::uint64_t>::max();

/**
 * @brief 파일 전체를 문자열로 읽는다.
 * @param path 읽을 파일 경로.
 * @param out 읽은 내용이 저장될 문자열.
 * @return 성공하면 true, 파일을 열 수 없으면 false.
 */
bool ReadEntireFile(const std::filesystem::path& path, std::string& out);
/**
 * @brief 파일 전체를 바이트 벡터로 읽는다.
 * @param path 읽을 파일 경로.
 * @param out 읽은 내용이 저장될 바이트 벡터.
 * @return 성공하면 true, 파일을 열 수 없으면 false.
 */
bool ReadEntireFile(const std::filesystem::path& path, std::vector<std::uint8_t>& out);
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
