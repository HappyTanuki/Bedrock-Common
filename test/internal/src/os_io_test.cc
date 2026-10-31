#include <platform/os_io.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <climits>
#include <cstring>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace bedrock::internal::os {
namespace {

#if defined(_WIN32)
HANDLE pending_lock_event = nullptr;
void ObservePendingLock() noexcept {
  if (pending_lock_event) SetEvent(pending_lock_event);
}
#endif

// UTF-8 spelling of a native path for the OS I/O interface. POSIX paths are
// passed through as bytes.
std::string Utf8(const std::filesystem::path& path) {
#if defined(_WIN32)
  const std::u8string text = path.u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
#else
  return path.string();
#endif
}

// A fresh directory owned by one test. It is created exclusively under the
// temporary directory, so concurrent runs (other processes, presets or
// compilers sharing TMPDIR) never share or delete each other's files, and only
// this directory is removed afterwards.
class TestDirectory final {
 public:
  TestDirectory() {
    std::error_code ec;
    const std::filesystem::path base = std::filesystem::temp_directory_path(ec);
    if (ec) {
      error_ = "temp_directory_path: " + ec.message();
      return;
    }
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string test = info != nullptr ? info->name() : "unknown";
#if defined(_WIN32)
    const unsigned long process = GetCurrentProcessId();
#else
    const long process = static_cast<long>(getpid());
#endif
    static std::atomic<unsigned> counter{0};
    for (int attempt = 0; attempt < 64; ++attempt) {
      const auto ticks =
          std::chrono::steady_clock::now().time_since_epoch().count();
      const std::filesystem::path candidate =
          base / ("bedrock-os-io-" + test + "-" + std::to_string(process) +
                  "-" + std::to_string(ticks) + "-" +
                  std::to_string(counter.fetch_add(1)));
      if (std::filesystem::create_directory(candidate, ec)) {
        path_ = candidate;
        return;
      }
      if (ec) {
        error_ = "create_directory " + Utf8(candidate) + ": " + ec.message();
        return;
      }
    }
    error_ = "no unused test directory name under " + Utf8(base);
  }
  ~TestDirectory() {
    if (path_.empty()) return;
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
    if (ec) {
      ADD_FAILURE() << "cannot remove test directory " << Utf8(path_)
                    << ": " << ec.message();
    }
  }
  TestDirectory(const TestDirectory&) = delete;
  TestDirectory& operator=(const TestDirectory&) = delete;

  [[nodiscard]] bool Ok() const noexcept { return !path_.empty(); }
  [[nodiscard]] const std::string& Error() const noexcept { return error_; }
  [[nodiscard]] const std::filesystem::path& Path() const noexcept {
    return path_;
  }
  [[nodiscard]] std::string Child(std::string_view name) const {
    return Utf8(path_ / std::filesystem::path(name));
  }

 private:
  std::filesystem::path path_;
  std::string error_;
};

TEST(OsIo, ValidateLocalDirectoryRequiresExistingAbsoluteLocalDirectory) {
  TestDirectory directory;
  ASSERT_TRUE(directory.Ok()) << directory.Error();
  const std::filesystem::path regular = directory.Path() / "regular-file";
  {
    std::ofstream out(regular, std::ios::binary);
    out << 'x';
    out.close();
    ASSERT_TRUE(out.good()) << "cannot create " << Utf8(regular);
  }
  const std::string local = Utf8(directory.Path());
  const std::string missing = directory.Child("missing-directory");
  // Child "invalid-" followed by the lone byte 0xFF, which is never UTF-8.
  std::string invalid_utf8 = directory.Child("invalid-");
  invalid_utf8.push_back(static_cast<char>(0xFF));

  struct Case {
    const char* name;
    std::string path;
    IoError expected;
  };
  const Case cases[] = {
      {"Relative", "relative/path", IoError::kInvalidArgument},
      {"Missing", missing, IoError::kNotFound},
      {"RegularFile", Utf8(regular), IoError::kInvalidArgument},
#if defined(_WIN32)
      // UNC and other non-drive forms are rejected before any network access.
      {"Unc", R"(\\server\share)", IoError::kInvalidArgument},
      {"UncForwardSlash", "//server/share", IoError::kInvalidArgument},
      {"DriveRelative", "C:relative", IoError::kInvalidArgument},
      // UTF-8 conversion fails before the (also missing) path is opened.
      {"InvalidUtf8", invalid_utf8, IoError::kInvalidArgument},
#endif
      {"LocalValid", local, IoError::kOk},
  };
  for (const Case& test : cases) {
    SCOPED_TRACE(test.name);
    const IoStatus status = ValidateLocalDirectory(test.path);
    EXPECT_EQ(status.code, test.expected)
        << "code " << static_cast<int>(status.code) << ", native "
        << status.native_error;
  }
#if !defined(_WIN32)
  {
    // POSIX paths are byte strings and are not UTF-8 validated; this absolute
    // path does not exist, so it must still be rejected.
    SCOPED_TRACE("InvalidUtf8");
    EXPECT_NE(ValidateLocalDirectory(invalid_utf8).code, IoError::kOk);
  }
#endif
  // Validation is read-only: it neither creates the missing directory nor
  // turns the regular file into a directory.
  std::error_code ec;
  EXPECT_FALSE(
      std::filesystem::exists(directory.Path() / "missing-directory", ec));
  EXPECT_FALSE(ec) << ec.message();
  EXPECT_TRUE(std::filesystem::is_regular_file(regular, ec));
  EXPECT_FALSE(ec) << ec.message();
}

TEST(OsIo, EnvironmentDistinguishesAbsentEmptyAndPreservesUtf8) {
  constexpr std::string_view absent_name = "BEDROCK_OS_IO_TEST_ABSENT_91F7";
#if defined(_WIN32)
  SetEnvironmentVariableW(L"BEDROCK_OS_IO_TEST_UTF8_91F7", nullptr);
#else
  unsetenv("BEDROCK_OS_IO_TEST_UTF8_91F7");
#endif
  std::size_t bytes = 77;
  std::array<char, 32> buffer{};
  auto status = ReadEnvironment(absent_name, buffer, bytes);
  EXPECT_EQ(status.code, IoError::kNotFound);

  constexpr std::string_view name = "BEDROCK_OS_IO_TEST_UTF8_91F7";
#if defined(_WIN32)
  SetEnvironmentVariableW(L"BEDROCK_OS_IO_TEST_UTF8_91F7", L"");
#else
  setenv("BEDROCK_OS_IO_TEST_UTF8_91F7", "", 1);
#endif
  status = ReadEnvironment(name, buffer, bytes);
  ASSERT_TRUE(status.Ok());
  EXPECT_EQ(bytes, 0u);

#if defined(_WIN32)
  // 좁은 문자 _putenv_s는 시스템 ANSI 코드페이지로 해석되므로 wide로 설정한다.
  SetEnvironmentVariableW(L"BEDROCK_OS_IO_TEST_UTF8_91F7", L"snowman \u2603");
#else
  setenv("BEDROCK_OS_IO_TEST_UTF8_91F7", "snowman \xE2\x98\x83", 1);
#endif
  status = ReadEnvironment(name, buffer, bytes);
  ASSERT_TRUE(status.Ok());
  EXPECT_EQ(std::string_view(buffer.data(), bytes), "snowman \xE2\x98\x83");
  std::array<char, 3> undersized{};
  undersized.fill('x');
  status = ReadEnvironment(name, undersized, bytes);
  EXPECT_EQ(status.code, IoError::kBufferTooSmall);
  EXPECT_EQ(bytes, 11u);
  EXPECT_EQ(undersized[0], 'x');
  EXPECT_EQ(ReadEnvironment(std::string_view("BAD\0NAME", 8), buffer, bytes).code,
            IoError::kInvalidArgument);
}

TEST(OsIo, IndependentlyOpenedHandlesContendAndOffsetIoIsExact) {
  TestDirectory directory;
  ASSERT_TRUE(directory.Ok()) << directory.Error();
  const std::string path = directory.Child("registry.tmp");
  File owner;
  const auto opened = Open(path, OpenMode::kReadWriteCreate, owner);
  ASSERT_TRUE(opened.status.Ok());
  ASSERT_TRUE(opened.created);
  File other;
  const auto existing = Open(path, OpenMode::kReadWriteCreate, other);
  ASSERT_TRUE(existing.status.Ok());
  EXPECT_FALSE(existing.created);
  constexpr std::array<std::byte, 4> content = {
      std::byte{'t'}, std::byte{'e'}, std::byte{'s'}, std::byte{'t'}};
  std::size_t transferred = 0;
  ASSERT_TRUE(WriteAt(owner, 8, content, transferred).Ok());
  ASSERT_EQ(transferred, content.size());
  ASSERT_TRUE(Flush(owner).Ok());
  std::array<std::byte, 8> read{};
  EXPECT_TRUE(ReadAt(other, 8, read, transferred).Ok());
  EXPECT_EQ(transferred, 4u);
  EXPECT_TRUE(std::equal(content.begin(), content.end(), read.begin()));

  ASSERT_TRUE(LockRange(owner, 0, 1, false).Ok());
  EXPECT_EQ(LockRange(other, 0, 1, false).code, IoError::kBusy);
  EXPECT_TRUE(LockRange(other, 1, 1, false).Ok());
  EXPECT_TRUE(UnlockRange(other, 1, 1).Ok());
  File unrelated;
  ASSERT_TRUE(Open(path, OpenMode::kReadWriteCreate, unrelated).status.Ok());
  unrelated.Close();
  EXPECT_EQ(LockRange(other, 0, 1, false).code, IoError::kBusy);
  owner.Close();
  EXPECT_TRUE(LockRange(other, 0, 1, false).Ok());
  EXPECT_TRUE(UnlockRange(other, 0, 1).Ok());
  other.Close();
}

// Windows additionally proves that LockFileEx entered ERROR_IO_PENDING. Other
// platforms check completion only; timing alone does not show that the
// waiting lock call actually blocked.
TEST(OsIo, WaitingLockCompletesAfterCompetingHandleUnlocks) {
  TestDirectory directory;
  ASSERT_TRUE(directory.Ok()) << directory.Error();
  const std::string path = directory.Child("pending-lock.tmp");
  File owner;
  File waiter;
  ASSERT_TRUE(Open(path, OpenMode::kReadWriteCreate, owner).status.Ok());
  ASSERT_TRUE(Open(path, OpenMode::kReadWriteCreate, waiter).status.Ok());
  ASSERT_TRUE(LockRange(owner, 0, 1, false).Ok());

  std::atomic<bool> entered{false};
  IoStatus result{IoError::kSystemError};
#if defined(_WIN32)
  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  ASSERT_NE(event, nullptr);
  pending_lock_event = event;
  SetPendingLockObserverForTesting(&ObservePendingLock);
#endif
  std::thread thread([&] {
    entered.store(true, std::memory_order_release);
    result = LockRange(waiter, 0, 1, true);
  });
  while (!entered.load(std::memory_order_acquire)) std::this_thread::yield();
#if defined(_WIN32)
  const DWORD observed = WaitForSingleObject(event, 5000);
  SetPendingLockObserverForTesting(nullptr);
  pending_lock_event = nullptr;
  CloseHandle(event);
#endif
  const auto unlock = UnlockRange(owner, 0, 1);
  thread.join();
#if defined(_WIN32)
  EXPECT_EQ(observed, WAIT_OBJECT_0) << "LockFileEx did not enter ERROR_IO_PENDING";
#endif
  EXPECT_TRUE(unlock.Ok());
  EXPECT_TRUE(result.Ok()) << static_cast<int>(result.code) << ", " << result.native_error;
  if (result.Ok()) EXPECT_TRUE(UnlockRange(waiter, 0, 1).Ok());
  owner.Close();
  waiter.Close();
}

TEST(OsIo, ClosedOutputAndInvalidRangeAreRejected) {
  File file;
  std::uint64_t size = 0;
  EXPECT_EQ(FileSize(file, size).code, IoError::kInvalidArgument);
  EXPECT_EQ(LockRange(file, UINT64_MAX, 2, false).code, IoError::kInvalidArgument);
  EXPECT_EQ(Open(std::string_view("bad\0path", 8), OpenMode::kReadWriteCreate, file)
                .status.code,
            IoError::kInvalidArgument);
  EXPECT_FALSE(file.IsOpen());
}

TEST(OsIo, OffsetIoRejectsOverflowWithoutTouchingFile) {
  TestDirectory directory;
  ASSERT_TRUE(directory.Ok()) << directory.Error();
  const std::string path = directory.Child("range.tmp");
  File file;
  ASSERT_TRUE(Open(path, OpenMode::kReadWriteCreate, file).status.Ok());
  constexpr std::array<std::byte, 2> data{std::byte{1}, std::byte{2}};
  std::size_t transferred = 99;
  EXPECT_EQ(WriteAt(file, static_cast<std::uint64_t>(INT64_MAX), data, transferred).code,
            IoError::kInvalidArgument);
  EXPECT_EQ(transferred, 0u);
  std::uint64_t size = 0;
  ASSERT_TRUE(FileSize(file, size).Ok());
  EXPECT_EQ(size, 0u);
  file.Close();
}

TEST(OsIo, MoveTransfersLockOwnershipExactlyOnce) {
  TestDirectory directory;
  ASSERT_TRUE(directory.Ok()) << directory.Error();
  const std::string path = directory.Child("move.tmp");
  File owner;
  File other;
  ASSERT_TRUE(Open(path, OpenMode::kReadWriteCreate, owner).status.Ok());
  ASSERT_TRUE(Open(path, OpenMode::kReadWriteCreate, other).status.Ok());
  ASSERT_TRUE(LockRange(owner, 4, 1, false).Ok());
  File moved(std::move(owner));
  EXPECT_FALSE(owner.IsOpen());
  EXPECT_TRUE(moved.IsOpen());
  EXPECT_EQ(LockRange(other, 4, 1, false).code, IoError::kBusy);
  moved.Close();
  EXPECT_TRUE(LockRange(other, 4, 1, false).Ok());
  EXPECT_TRUE(UnlockRange(other, 4, 1).Ok());
  other.Close();
}

TEST(OsIo, SlotRangeBeyondEofPreservesMetadataReadsAndFileSize) {
  TestDirectory directory;
  ASSERT_TRUE(directory.Ok()) << directory.Error();
  const std::string path = directory.Child("slot.tmp");
  File owner;
  File observer;
  ASSERT_TRUE(Open(path, OpenMode::kReadWriteCreate, owner).status.Ok());
  constexpr std::array<std::byte, 1> initial{std::byte{0x42}};
  std::size_t transferred = 0;
  ASSERT_TRUE(WriteAt(owner, 0, initial, transferred).Ok());
  ASSERT_EQ(transferred, 1u);
  ASSERT_TRUE(Open(path, OpenMode::kReadWriteCreate, observer).status.Ok());
  ASSERT_TRUE(LockRange(owner, 192, 1, false).Ok());
  EXPECT_EQ(LockRange(observer, 192, 1, false).code, IoError::kBusy);
  std::array<std::byte, 1> read{};
  ASSERT_TRUE(ReadAt(observer, 0, read, transferred).Ok());
  EXPECT_EQ(transferred, 1u);
  EXPECT_EQ(read[0], initial[0]);
  std::uint64_t size = 0;
  ASSERT_TRUE(FileSize(observer, size).Ok());
  EXPECT_EQ(size, 1u);
  EXPECT_TRUE(UnlockRange(owner, 192, 1).Ok());
  owner.Close();
  observer.Close();
}

}  // namespace
}  // namespace bedrock::internal::os
