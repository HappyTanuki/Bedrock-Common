#include "archive/node.h"
#include "archive/yaml/serialize.h"
#include "archive/yaml/present.h"
#include "archive/transcriber.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <system_error>

namespace bedrock::archive::transcriber {

TEST(ArchiveIdentity, NegativeIdentitiesAreNeverAliases) {
  Node failed_a;
  Node failed_b;
  failed_a.identity = -7;
  failed_b.identity = -7;

  EXPECT_FALSE(failed_a.SameIdentity(failed_b));
}

TEST(ArchiveIdentity, FindsNegativeIdentityInMappingKeysAndValues) {
  Node root;
  root.identity = 0;
  root.kind = Node::Kind::kMapping;
  Node::Pair pair;
  pair.key.identity = -8;
  pair.value.identity = -9;
  root.pairs.push_back(pair);

  EXPECT_EQ(FindIdentityError(root), -8);
}

TEST(ArchiveIdentity, FindsNegativeIdentityInNestedSequence) {
  Node root;
  root.identity = 0;
  root.kind = Node::Kind::kSequence;
  Node child;
  child.identity = -11;
  root.items.push_back(child);

  EXPECT_EQ(FindIdentityError(root), -11);
}

TEST(ArchiveIdentity, NegativeSerializeReturnsEmptyStreamAndPresentFails) {
  Node root;
  root.identity = 0;
  root.kind = Node::Kind::kSequence;
  Node child;
  child.identity = -1;
  root.items.push_back(child);
  const auto serialized = bedrock::archive::yaml::Serialize(root);
  EXPECT_EQ(serialized.identity_error, -1);
  EXPECT_TRUE(serialized.documents.empty());
  const auto presented = bedrock::archive::yaml::Present(serialized);
  EXPECT_FALSE(presented.ok);
  EXPECT_TRUE(presented.text.empty());
}

// Every SnowflakeError, by enumerator name, maps to the TranscriberError and
// public bedrock::ErrorCode of the same name (exact values pinned as well);
// values outside -16..-1 are generic and non-negative identities succeed.
TEST(ArchiveIdentity, SnowflakeMapperBoundsAndZero) {
  using bedrock::ErrorCode;
  using bedrock::SnowflakeError;
  using bedrock::archive::transcriber::SnowflakeIdentityError;
  struct Mapping {
    SnowflakeError error;
    std::int64_t identity;  // exact signed Generate() result
    TranscriberError transcriber;
    ErrorCode code;
    std::uint32_t value;  // exact public value
  };
  const Mapping mappings[] = {
      {SnowflakeError::kMissingMachineId, -1, TranscriberError::kSnowflakeMissingMachineId,
       ErrorCode::kSnowflakeMissingMachineId, 0x101},
      {SnowflakeError::kInvalidMachineId, -2, TranscriberError::kSnowflakeInvalidMachineId,
       ErrorCode::kSnowflakeInvalidMachineId, 0x102},
      {SnowflakeError::kInvalidStatePath, -3, TranscriberError::kSnowflakeInvalidStatePath,
       ErrorCode::kSnowflakeInvalidStatePath, 0x103},
      {SnowflakeError::kEnvironmentReadFailed, -4,
       TranscriberError::kSnowflakeEnvironmentReadFailed,
       ErrorCode::kSnowflakeEnvironmentReadFailed, 0x104},
      {SnowflakeError::kConfigurationReadFailed, -5,
       TranscriberError::kSnowflakeConfigurationReadFailed,
       ErrorCode::kSnowflakeConfigurationReadFailed, 0x105},
      {SnowflakeError::kRegistryIoFailed, -6, TranscriberError::kSnowflakeRegistryIoFailed,
       ErrorCode::kSnowflakeRegistryIoFailed, 0x106},
      {SnowflakeError::kRegistryCorrupt, -7, TranscriberError::kSnowflakeRegistryCorrupt,
       ErrorCode::kSnowflakeRegistryCorrupt, 0x107},
      {SnowflakeError::kRegistryIncompatible, -8,
       TranscriberError::kSnowflakeRegistryIncompatible,
       ErrorCode::kSnowflakeRegistryIncompatible, 0x108},
      {SnowflakeError::kMachineIdMismatch, -9, TranscriberError::kSnowflakeMachineIdMismatch,
       ErrorCode::kSnowflakeMachineIdMismatch, 0x109},
      {SnowflakeError::kLockFailed, -10, TranscriberError::kSnowflakeLockFailed,
       ErrorCode::kSnowflakeLockFailed, 0x10A},
      {SnowflakeError::kSlotsExhausted, -11, TranscriberError::kSnowflakeSlotsExhausted,
       ErrorCode::kSnowflakeSlotsExhausted, 0x10B},
      {SnowflakeError::kClockBeforeEpoch, -12, TranscriberError::kSnowflakeClockBeforeEpoch,
       ErrorCode::kSnowflakeClockBeforeEpoch, 0x10C},
      {SnowflakeError::kClockOutOfRange, -13, TranscriberError::kSnowflakeClockOutOfRange,
       ErrorCode::kSnowflakeClockOutOfRange, 0x10D},
      {SnowflakeError::kClockRollback, -14, TranscriberError::kSnowflakeClockRollback,
       ErrorCode::kSnowflakeClockRollback, 0x10E},
      {SnowflakeError::kSequenceExhausted, -15, TranscriberError::kSnowflakeSequenceExhausted,
       ErrorCode::kSnowflakeSequenceExhausted, 0x10F},
      {SnowflakeError::kUnsupportedPlatform, -16,
       TranscriberError::kSnowflakeUnsupportedPlatform,
       ErrorCode::kSnowflakeUnsupportedPlatform, 0x110},
  };
  std::set<std::int64_t> identities;
  for (const Mapping& mapping : mappings) {
    SCOPED_TRACE(mapping.identity);
    ASSERT_EQ(static_cast<std::int64_t>(mapping.error), mapping.identity);
    ASSERT_EQ(static_cast<std::uint32_t>(mapping.transcriber), mapping.value);
    ASSERT_EQ(static_cast<std::uint32_t>(mapping.code), mapping.value);
    identities.insert(mapping.identity);
    const std::error_code mapped = SnowflakeIdentityError(mapping.identity);
    EXPECT_EQ(&mapped.category(), &TranscriberCategory());
    EXPECT_EQ(mapped, make_error_code(mapping.transcriber));
    std::string storage;
    const bedrock::Status status = BorrowStatus(OwnedStatus(mapped), storage);
    EXPECT_TRUE(status.Failed());
    EXPECT_EQ(static_cast<std::uint32_t>(status.code), mapping.value);
  }
  EXPECT_EQ(identities.size(), 16u);
  EXPECT_EQ(*identities.begin(), -16);
  EXPECT_EQ(*identities.rbegin(), -1);

  for (const std::int64_t generic : {std::int64_t{-17}, std::numeric_limits<std::int64_t>::min()}) {
    SCOPED_TRACE(generic);
    const std::error_code mapped = SnowflakeIdentityError(generic);
    EXPECT_EQ(mapped, make_error_code(TranscriberError::kError));
    EXPECT_EQ(static_cast<std::uint32_t>(mapped.value()), 0xFFFFFFFFu);
    std::string storage;
    EXPECT_EQ(static_cast<std::uint32_t>(BorrowStatus(OwnedStatus(mapped), storage).code),
              static_cast<std::uint32_t>(ErrorCode::kError));
  }
  for (const std::int64_t valid : {std::int64_t{0}, std::numeric_limits<std::int64_t>::max()}) {
    SCOPED_TRACE(valid);
    const std::error_code mapped = SnowflakeIdentityError(valid);
    EXPECT_FALSE(mapped);
    EXPECT_EQ(mapped.value(), 0);
    std::string storage;
    const bedrock::Status status = BorrowStatus(OwnedStatus(mapped), storage);
    EXPECT_TRUE(status.Ok());
    EXPECT_EQ(static_cast<std::uint32_t>(status.code),
              static_cast<std::uint32_t>(ErrorCode::kSuccess));
  }
}

TEST(ArchiveIdentity, ZeroIdentityIsValidAndAliasesItself) {
  Node zero_a;
  Node zero_b;
  zero_a.identity = 0;
  zero_b.identity = 0;

  EXPECT_TRUE(zero_a.SameIdentity(zero_b));
  EXPECT_EQ(FindIdentityError(zero_a), 0);
}

}  // namespace bedrock::archive::transcriber
