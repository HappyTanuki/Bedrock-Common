/**
 * @file parse.cc
 * @brief RBF Parse(바이트 -> 이벤트) 구현부.
 *
 * 매직을 확인하고 노드 하나를 재귀적으로 훑어 이벤트를 방출한다. 컨테이너는
 * 시작/끝 마커로 감싸고, 스칼라는 값(널·vtype·바이트)을 실은 kScalar 이벤트로
 * 낸다. 손상 입력 방어: 개수/길이는 남은 바이트로 상한, 중첩 깊이 상한.
 */
#include "archive/rbf/parse.h"

#include <cstdint>

#include "archive/rbf/format.h"

namespace bedrock::archive::rbf {

namespace {

/** @brief 바이트열을 훑으며 이벤트를 방출하는 커서 기반 파서. */
struct Parser {
  std::span<const std::uint8_t> data;
  std::size_t pos = 0;
  std::vector<Event> events;
  bool ok = true;
  std::string error;

  /** @brief 중첩 깊이 상한(스택 오버플로 방어). */
  static constexpr int kMaxDepth = 100;

  void Fail(const char* message) {
    if (ok) {
      ok = false;
      error = std::string("rbf: ") + message;
    }
  }
  bool Need(std::size_t index) {
    if (pos + index > data.size()) {
      Fail("underflow");
      return false;
    }
    return true;
  }
  std::uint8_t GetU8() {
    if (!Need(1)) {
      return 0;
    }
    return data[pos++];
  }
  std::uint64_t GetVarint() {
    std::uint64_t result = 0;
    int shift = 0;
    for (;;) {
      if (!Need(1)) {
        return 0;
      }
      const std::uint8_t byte_value = data[pos++];
      result |= static_cast<std::uint64_t>(byte_value & 0x7FU) << shift;
      if ((byte_value & 0x80U) == 0) {
        break;
      }
      shift += 7;
      if (shift > 63) {
        Fail("varint too long");
        return 0;
      }
    }
    return result;
  }
  /** @brief 남은 바이트로 상한을 둔 길이/개수. */
  std::size_t BoundedLen() {
    const std::uint64_t node = GetVarint();
    if (!ok) {
      return 0;
    }
    if (node > data.size() - pos) {
      Fail("length exceeds input");
      return 0;
    }
    return static_cast<std::size_t>(node);
  }
  std::string GetRaw(std::size_t node) {
    if (!Need(node)) {
      return {};
    }
    std::string scalar;
    scalar.reserve(node);
    for (std::size_t i = 0; i < node; ++i) {
      scalar.push_back(static_cast<char>(data[pos + i]));
    }
    pos += node;
    return scalar;
  }

  void ParseNode(int depth) {
    if (!ok) {
      return;
    }
    if (depth > kMaxDepth) {
      Fail("max depth");
      return;
    }
    if (!Need(1)) {
      return;
    }
    const std::uint8_t index = data[pos++];
    if (index == static_cast<std::uint8_t>(BinKind::kScalar)) {
      Event event;
      event.kind = EventKind::kScalar;
      event.null = GetU8() != 0;
      event.vtype = static_cast<transcriber::ValueType>(
          static_cast<std::uint32_t>(GetVarint()));
      const std::size_t len = BoundedLen();
      if (!ok) {
        return;
      }
      event.scalar = GetRaw(len);
      if (!ok) {
        return;
      }
      events.push_back(std::move(event));
      return;
    }
    if (index == static_cast<std::uint8_t>(BinKind::kSequence) ||
        index == static_cast<std::uint8_t>(BinKind::kSet)) {
      const bool is_seq =
          index == static_cast<std::uint8_t>(BinKind::kSequence);
      events.push_back(
          Event{is_seq ? EventKind::kSeqStart : EventKind::kSetStart,
                false,
                transcriber::ValueType::kNull,
                {}});
      const std::size_t count = BoundedLen();
      if (!ok) {
        return;
      }
      for (std::size_t i = 0; i < count && ok; ++i) {
        ParseNode(depth + 1);
      }
      if (!ok) {
        return;
      }
      events.push_back(Event{is_seq ? EventKind::kSeqEnd : EventKind::kSetEnd,
                             false,
                             transcriber::ValueType::kNull,
                             {}});
      return;
    }
    if (index == static_cast<std::uint8_t>(BinKind::kMapping)) {
      events.push_back(Event{
          EventKind::kMapStart, false, transcriber::ValueType::kNull, {}});
      const std::size_t count = BoundedLen();
      if (!ok) {
        return;
      }
      for (std::size_t i = 0; i < count && ok; ++i) {
        ParseNode(depth + 1);  // 키
        ParseNode(depth + 1);  // 값
      }
      if (!ok) {
        return;
      }
      events.push_back(
          Event{EventKind::kMapEnd, false, transcriber::ValueType::kNull, {}});
      return;
    }
    Fail("bad kind");
  }
};

}  // namespace

ParseResult Parse(std::span<const std::uint8_t> bytes) {
  ParseResult out;
  if (bytes.size() < kBinMagic.size()) {
    out.error = "rbf: magic mismatch";
    return out;
  }
  for (std::size_t i = 0; i < kBinMagic.size(); ++i) {
    if (bytes[i] != kBinMagic[i]) {
      out.error = "rbf: magic mismatch";
      return out;
    }
  }
  Parser pair{bytes};
  pair.pos = kBinMagic.size();
  pair.ParseNode(0);
  if (!pair.ok) {
    out.error = std::move(pair.error);
    return out;
  }
  out.ok = true;
  out.events = std::move(pair.events);
  return out;
}

}  // namespace bedrock::archive::rbf
