#include <gtest/gtest.h>
#include <windows.h>

#include <cstdint>
#include <thread>

namespace {
struct Library {
  HMODULE handle = nullptr;
  explicit Library(const wchar_t* name) : handle(LoadLibraryW(name)) {}
  ~Library() {
    if (handle != nullptr) FreeLibrary(handle);
  }
  Library(const Library&) = delete;
  Library& operator=(const Library&) = delete;
};
constexpr int MachineOf(std::int64_t id) { return static_cast<int>((id >> 19) & 7); }
constexpr int SlotOf(std::int64_t id) { return static_cast<int>((id >> 12) & 127); }
}  // namespace

TEST(SnowflakeStaticCopies, IndependentlyLinkedDllsShareRegistryOnOneThread) {
  const Library a(L"snowflake_static_copy_a.dll");
  ASSERT_NE(a.handle, nullptr) << GetLastError();
  const Library b(L"snowflake_static_copy_b.dll");
  ASSERT_NE(b.handle, nullptr) << GetLastError();
  using Generate = std::int64_t (*)() noexcept;
  const auto generate_a =
      reinterpret_cast<Generate>(GetProcAddress(a.handle, "SnowflakeStaticCopyGenerate"));
  const auto generate_b =
      reinterpret_cast<Generate>(GetProcAddress(b.handle, "SnowflakeStaticCopyGenerate"));
  ASSERT_NE(generate_a, nullptr);
  ASSERT_NE(generate_b, nullptr);
  std::int64_t x = -99, y = -99;
  // One issuing thread calls both copies; joining it ends each copy's
  // thread-local issuer (and slot handle) while both DLLs are still loaded.
  std::thread issuer([&] {
    x = generate_a();
    y = generate_b();
  });
  issuer.join();
  ASSERT_GE(x, 0);
  ASSERT_GE(y, 0);
  EXPECT_EQ(MachineOf(x), 0) << "test fixture machine id";
  EXPECT_EQ(MachineOf(y), 0) << "test fixture machine id";
  EXPECT_NE(SlotOf(x), SlotOf(y)) << "independent copies must not share a slot";
  EXPECT_NE(x, y);
}
