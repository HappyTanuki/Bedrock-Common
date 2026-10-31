#include "common/data_types/snowflake.h"
#include "data_types/snowflake_internal.h"
#include "platform/os_io.h"
#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <span>
namespace {
using bedrock::SnowflakeError; using bedrock::internal::snowflake::IssueAt; using bedrock::internal::snowflake::IssuerState;
using bedrock::internal::os::IoError;
using bedrock::internal::os::IoStatus;
using bedrock::internal::snowflake::CompleteAcquisitionFence;
using bedrock::internal::snowflake::MapLockFailure;
constexpr std::int64_t epoch=1704067200000LL;

constexpr std::int64_t Code(SnowflakeError error) {
  return static_cast<std::int64_t>(error);
}

constexpr std::int64_t Id(std::int64_t delta_ms, std::int64_t machine,
                          std::int64_t slot, std::int64_t sequence) {
  return (delta_ms << 22) | (machine << 19) | (slot << 12) | sequence;
}

void ExpectSameState(const IssuerState& actual, const IssuerState& expected) {
  EXPECT_EQ(actual.observed_ms, expected.observed_ms);
  EXPECT_EQ(actual.issued_ms, expected.issued_ms);
  EXPECT_EQ(actual.sequence, expected.sequence);
  EXPECT_EQ(actual.observed, expected.observed);
  EXPECT_EQ(actual.issued, expected.issued);
}

// Scripted clock for CompleteAcquisitionFence. Sampling past the end of the
// script is a test failure; the out-of-script sample lies before the epoch so
// the fence stops with an error instead of reading beyond the script.
struct ClockScript {
  explicit ClockScript(std::span<const std::int64_t> values) noexcept
      : samples(values) {}
  std::span<const std::int64_t> samples;
  std::size_t next = 0;
  bool overrun = false;
};

std::int64_t SampleScript(void* context) {
  auto& script = *static_cast<ClockScript*>(context);
  if (script.next >= script.samples.size()) {
    script.overrun = true;
    ADD_FAILURE() << "acquisition fence sampled the clock more than the "
                  << script.samples.size() << " scripted times";
    return 0;
  }
  return script.samples[script.next++];
}

TEST(SnowflakeCore, IssueAtZeroAndBoundaries) {
 IssuerState s;
 EXPECT_EQ(IssueAt(s,7,127,epoch+1),((std::int64_t{1}<<22)|(std::int64_t{7}<<19)|(std::int64_t{127}<<12)));
 IssuerState max; const std::int64_t base=(std::int64_t{2199023255551LL}<<22)|(std::int64_t{7}<<19)|(std::int64_t{127}<<12); for(int i=0;i<4096;++i) EXPECT_EQ(IssueAt(max,7,127,epoch+2199023255551LL),base+i);
 EXPECT_EQ(IssueAt(max,7,127,epoch+2199023255552LL),(std::int64_t)SnowflakeError::kClockOutOfRange);
}

TEST(SnowflakeCore, ZeroIsAValidGeneratedIdentity) {
  // Machine 0, slot 0 and sequence 0 at exactly the epoch encode to 0. Zero is
  // a valid identity; only negative results are errors.
  IssuerState state;
  EXPECT_EQ(IssueAt(state, 0, 0, epoch), 0);
  EXPECT_TRUE(state.issued);
  EXPECT_EQ(state.issued_ms, epoch);
  EXPECT_EQ(state.sequence, 0);
  // Zero was consumed as a real identity, so the same tick continues at 1.
  EXPECT_EQ(IssueAt(state, 0, 0, epoch), 1);
}

TEST(SnowflakeCore, SequenceExhaustionAndRecovery) {
  IssuerState s;
  for (std::int64_t sequence = 0; sequence <= 4095; ++sequence) {
    ASSERT_EQ(IssueAt(s, 2, 9, epoch), Id(0, 2, 9, sequence));
  }
  const IssuerState exhausted = s;
  // Exhaustion is reported immediately and repeatedly within the same tick:
  // no hidden wait, retry, sequence wrap or borrowed future timestamp.
  for (int attempt = 0; attempt < 3; ++attempt) {
    SCOPED_TRACE(attempt);
    EXPECT_EQ(IssueAt(s, 2, 9, epoch),
              Code(SnowflakeError::kSequenceExhausted));
    ExpectSameState(s, exhausted);
  }
  EXPECT_EQ(IssueAt(s, 2, 9, epoch + 1), Id(1, 2, 9, 0));
  EXPECT_EQ(s.issued_ms, epoch + 1);
  EXPECT_EQ(s.sequence, 0);
}

TEST(SnowflakeCore, RollbackPreservesHighWaterAndSequence) {
 IssuerState s; EXPECT_EQ(IssueAt(s,0,0,epoch+4),std::int64_t{4}<<22);
 EXPECT_EQ(IssueAt(s,0,0,epoch+3),(std::int64_t)SnowflakeError::kClockRollback);
 EXPECT_EQ(s.issued_ms,epoch+4); EXPECT_EQ(s.sequence,0); EXPECT_EQ(s.observed_ms,epoch+4);
 EXPECT_EQ(IssueAt(s,0,0,epoch+5),std::int64_t{5}<<22);
}

TEST(SnowflakeCore, AcquisitionFenceWaitsAcrossSameMillisecondSamples) {
  const std::int64_t samples[] = {epoch + 5, epoch + 5, epoch + 6};
  ClockScript script(samples);
  IssuerState state;
  EXPECT_EQ(CompleteAcquisitionFence(state, epoch + 5, SampleScript, &script),
            0);
  EXPECT_EQ(script.next, 3u);
  EXPECT_FALSE(script.overrun);
  EXPECT_TRUE(state.observed);
  EXPECT_EQ(state.observed_ms, epoch + 6);
  EXPECT_EQ(IssueAt(state, 3, 4, epoch + 5),
            Code(SnowflakeError::kClockRollback));
  EXPECT_EQ(IssueAt(state, 3, 4, epoch + 7), Id(7, 3, 4, 0));
}

TEST(SnowflakeCore, AcquisitionFenceBackwardSampleErrorsWithoutChangingFence) {
  const std::int64_t samples[] = {epoch + 4};
  ClockScript script(samples);
  IssuerState state;
  EXPECT_EQ(CompleteAcquisitionFence(state, epoch + 5, SampleScript, &script),
            Code(SnowflakeError::kClockRollback));
  EXPECT_EQ(script.next, 1u);
  EXPECT_FALSE(script.overrun);
  EXPECT_FALSE(state.observed);
}

TEST(SnowflakeCore, AcquisitionFenceRetryCannotBypassFailedFence) {
  constexpr std::int64_t initial = epoch + 100;
  IssuerState state;
  const std::int64_t backward[] = {initial - 1};
  ClockScript failed(backward);
  EXPECT_EQ(CompleteAcquisitionFence(state, initial, SampleScript, &failed),
            Code(SnowflakeError::kClockRollback));
  EXPECT_EQ(failed.next, 1u);
  EXPECT_FALSE(failed.overrun);
  EXPECT_FALSE(state.observed);
  EXPECT_FALSE(state.issued);

  // The retry reuses the same issuer state and acquisition sample. A sample
  // equal to the acquisition sample must not complete the fence.
  const std::int64_t retry_samples[] = {initial, initial + 1};
  ClockScript retry(retry_samples);
  EXPECT_EQ(CompleteAcquisitionFence(state, initial, SampleScript, &retry), 0);
  EXPECT_EQ(retry.next, 2u);
  EXPECT_FALSE(retry.overrun);
  EXPECT_TRUE(state.observed);
  EXPECT_EQ(state.observed_ms, initial + 1);
  EXPECT_FALSE(state.issued);

  // Ticks at or before the fence cannot be issued after the retry.
  EXPECT_EQ(IssueAt(state, 1, 2, initial),
            Code(SnowflakeError::kClockRollback));
  EXPECT_EQ(IssueAt(state, 1, 2, initial - 1),
            Code(SnowflakeError::kClockRollback));
  EXPECT_FALSE(state.issued);
  EXPECT_EQ(IssueAt(state, 1, 2, initial + 1), Id(101, 1, 2, 0));
}

TEST(SnowflakeCore, LockFailureMapsToLockFailed) {
  static_assert(Code(SnowflakeError::kLockFailed) == -10);
  // Every failed lock status except kUnsupported is a lock failure. The slot
  // scan skips kBusy before mapping; a waiting gate lock that still reports
  // kBusy has failed.
  const IoStatus failures[] = {
      {IoError::kSystemError, 5},     {IoError::kAccessDenied, 0},
      {IoError::kInvalidArgument, 0}, {IoError::kNotFound, 0},
      {IoError::kBufferTooSmall, 0},  {IoError::kBusy, 0},
  };
  for (const IoStatus& failure : failures) {
    SCOPED_TRACE(static_cast<int>(failure.code));
    EXPECT_EQ(Code(MapLockFailure(failure)), -10);
  }
}

TEST(SnowflakeCore, LockFailureUnsupportedMapsToUnsupportedPlatform) {
  static_assert(Code(SnowflakeError::kUnsupportedPlatform) == -16);
  EXPECT_EQ(Code(MapLockFailure({IoError::kUnsupported, 0})), -16);
  EXPECT_EQ(Code(MapLockFailure({IoError::kUnsupported, 50})), -16);
}
}
