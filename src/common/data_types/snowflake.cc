#include "common/data_types/snowflake.h"
#include "data_types/snowflake_internal.h"
#include "platform/os_io.h"
#include <array>
#include <chrono>
#include <cstring>
#include <thread>

namespace bedrock::internal::snowflake {
namespace {
constexpr std::int64_t kEpoch = 1704067200000LL;
constexpr std::int64_t kMaxDelta = 2199023255551LL;
constexpr std::int64_t Err(SnowflakeError e) { return static_cast<std::int64_t>(e); }
}
std::int64_t IssueAt(IssuerState& s, std::uint8_t machine, std::uint8_t slot,
                     std::int64_t unix_ms) noexcept {
  if (unix_ms < kEpoch) return Err(SnowflakeError::kClockBeforeEpoch);
  const auto delta = unix_ms - kEpoch;
  if (delta > kMaxDelta) return Err(SnowflakeError::kClockOutOfRange);
  if (s.observed && unix_ms < s.observed_ms) return Err(SnowflakeError::kClockRollback);
  if (!s.observed || unix_ms > s.observed_ms) { s.observed_ms = unix_ms; s.observed = true; }
  if (s.issued && unix_ms < s.issued_ms) return Err(SnowflakeError::kClockRollback);
  std::uint16_t seq = 0;
  if (s.issued && unix_ms == s.issued_ms) {
    if (s.sequence == 4095) return Err(SnowflakeError::kSequenceExhausted);
    seq = static_cast<std::uint16_t>(s.sequence + 1);
  }
  s.issued_ms = unix_ms; s.sequence = seq; s.issued = true;
  const std::uint64_t value = (static_cast<std::uint64_t>(delta) << 22) |
      (static_cast<std::uint64_t>(machine) << 19) |
      (static_cast<std::uint64_t>(slot) << 12) | seq;
  return static_cast<std::int64_t>(value);
}
std::int64_t CompleteAcquisitionFence(IssuerState& s, std::int64_t initial_ms,
                                      ClockSample sample, void* context) noexcept {
  if (!sample) return Err(SnowflakeError::kInvalidStatePath);
  for (;;) {
    const auto now = sample(context);
    if (now < kEpoch) return Err(SnowflakeError::kClockBeforeEpoch);
    if (now - kEpoch > kMaxDelta) return Err(SnowflakeError::kClockOutOfRange);
    if (now < initial_ms || (s.observed && now < s.observed_ms))
      return Err(SnowflakeError::kClockRollback);
    if (now == initial_ms) {
      std::this_thread::yield();
      continue;
    }
    s.observed = true;
    s.observed_ms = now;
    return 0;
  }
}
SnowflakeError MapLockFailure(os::IoStatus status) noexcept {
  return status.code == os::IoError::kUnsupported
             ? SnowflakeError::kUnsupportedPlatform
             : SnowflakeError::kLockFailed;
}
}
namespace bedrock {
namespace {
using namespace internal;
struct Config { std::int64_t error = 0; std::uint8_t machine = 0; std::array<char, 2048> dir{}; };
SnowflakeError IoErr(os::IoStatus s) {
  using E = os::IoError;
  if (s.code == E::kUnsupported) return SnowflakeError::kUnsupportedPlatform;
  return s.code == E::kInvalidArgument ? SnowflakeError::kInvalidStatePath : SnowflakeError::kRegistryIoFailed;
}
bool Eq(const std::byte* b, std::size_t n, const char* s) { return std::memcmp(b,s,n)==0; }
void Put(std::byte* b, std::uint64_t v, unsigned n) { for(unsigned i=0;i<n;++i)b[i]=static_cast<std::byte>((v>>(8*i))&255); }
std::uint64_t Get(const std::byte* b,unsigned n){std::uint64_t v=0;for(unsigned i=0;i<n;++i)v|=static_cast<std::uint64_t>(std::to_integer<unsigned char>(b[i]))<<(8*i);return v;}
Config LoadConfig() noexcept {
  Config c; std::array<char,2048> tmp{}; std::size_t n=0;
  auto st=os::ReadEnvironment("BEDROCK_COMMON_STATE_DIR",tmp,n);
  bool explicit_dir=st.code==os::IoError::kOk;
  if(!explicit_dir && st.code!=os::IoError::kNotFound){c.error=static_cast<std::int64_t>((st.code==os::IoError::kBufferTooSmall?SnowflakeError::kInvalidStatePath:SnowflakeError::kEnvironmentReadFailed));return c;}
  if(explicit_dir){if(n==0||n>=tmp.size()||std::memchr(tmp.data(),0,n)){c.error=static_cast<std::int64_t>(SnowflakeError::kInvalidStatePath);return c;} std::memcpy(c.dir.data(),tmp.data(),n);}
  else {
#if defined(_WIN32)
    std::array<char,1024> root{}; std::size_t rn=0; st=os::SharedDataDirectory(root,rn);
    if(!st.Ok()||rn+15>=c.dir.size()){c.error=static_cast<std::int64_t>(SnowflakeError::kConfigurationReadFailed);return c;}
    std::memcpy(c.dir.data(),root.data(),rn); std::memcpy(c.dir.data()+rn,"\\Bedrock\\Common",16);
#else
    std::memcpy(c.dir.data(),"/var/lib/bedrock-common",24);
#endif
  }
  st=os::ValidateLocalDirectory(c.dir.data());
  if(!st.Ok()){c.error=static_cast<std::int64_t>((st.code==os::IoError::kUnsupported?SnowflakeError::kUnsupportedPlatform:SnowflakeError::kInvalidStatePath));return c;}
  tmp={}; n=0; st=os::ReadEnvironment("BEDROCK_SNOWFLAKE_MACHINE_ID",tmp,n);
  if(st.code==os::IoError::kNotFound){ char path[2100]{}; std::size_t d=std::strlen(c.dir.data()); if(d+12>=sizeof(path)){c.error=static_cast<std::int64_t>(SnowflakeError::kInvalidStatePath);return c;} std::memcpy(path,c.dir.data(),d); const char* sep="/machine-id";
#if defined(_WIN32)
    sep="\\machine-id";
#endif
    std::memcpy(path+d,sep,12); os::File f; auto o=os::Open(std::string_view(path,d+11),os::OpenMode::kReadOnly,f);
    if(!o.status.Ok()){c.error=static_cast<std::int64_t>((o.status.code==os::IoError::kNotFound?SnowflakeError::kMissingMachineId:SnowflakeError::kConfigurationReadFailed));return c;}
    std::size_t got=0; st=os::ReadAt(f,0,std::as_writable_bytes(std::span(tmp)),got); if(!st.Ok()||got>=tmp.size()){c.error=static_cast<std::int64_t>(SnowflakeError::kConfigurationReadFailed);return c;} n=got;
  } else if(!st.Ok()){c.error=static_cast<std::int64_t>(SnowflakeError::kEnvironmentReadFailed);return c;}
  std::size_t a=0,b=n; auto ws=[](char x){return x==' '||x=='\t'||x=='\n'||x=='\r'||x=='\v'||x=='\f';}; while(a<b&&ws(tmp[a]))++a; while(b>a&&ws(tmp[b-1]))--b;
  if(b-a!=1||tmp[a]<'0'||tmp[a]>'7'){c.error=static_cast<std::int64_t>(SnowflakeError::kInvalidMachineId);return c;} c.machine=static_cast<std::uint8_t>(tmp[a]-'0'); return c;
}
const Config& GetConfig(){static const Config c=LoadConfig();return c;}
std::int64_t Now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
struct Issuer { os::File file; std::uint8_t slot=0; internal::snowflake::IssuerState state{}; std::int64_t fence_initial=0; bool fence_complete=false; bool ready=false; };
thread_local Issuer issuer;
std::int64_t SampleNow(void*) noexcept { return Now(); }
std::int64_t FinishFence() noexcept {
  if (issuer.fence_complete) return 0;
  const auto result = internal::snowflake::CompleteAcquisitionFence(
      issuer.state, issuer.fence_initial, SampleNow, nullptr);
  if (result == 0) { issuer.fence_complete = true; issuer.ready = true; }
  return result;
}
std::int64_t Register(const Config& c) noexcept {
  if (issuer.file.IsOpen()) return FinishFence();
  char gatepath[2100]{}, regpath[2100]{}; const auto d=std::strlen(c.dir.data()); if(d+32>=sizeof(gatepath))return static_cast<std::int64_t>(SnowflakeError::kInvalidStatePath);
  std::memcpy(gatepath,c.dir.data(),d); std::memcpy(regpath,c.dir.data(),d);
#if defined(_WIN32)
  const char *gate="\\snowflake.registry.lock",*reg="\\snowflake.registry";
#else
  const char *gate="/snowflake.registry.lock",*reg="/snowflake.registry";
#endif
  const auto gl=std::strlen(gate),rl=std::strlen(reg); std::memcpy(gatepath+d,gate,gl+1);std::memcpy(regpath+d,reg,rl+1);
  os::File gatefile, rf; auto go=os::Open(gatepath,os::OpenMode::kReadWriteCreate,gatefile); if(!go.status.Ok())return static_cast<std::int64_t>(IoErr(go.status));
  // The registration gate waits for its lock, so every failure (including a
  // busy result) is a lock failure rather than registry I/O.
  const auto lk = os::LockRange(gatefile, 0, 1, true);
  if (!lk.Ok()) {
    return static_cast<std::int64_t>(internal::snowflake::MapLockFailure(lk));
  }
  auto ro=os::Open(regpath,os::OpenMode::kReadWriteCreate,rf); if(!ro.status.Ok())return static_cast<std::int64_t>(IoErr(ro.status));
  std::array<std::byte,192> buf{};
  if(ro.created){ const char magic[]="BRSNFLK1";std::memcpy(buf.data(),magic,8);Put(buf.data()+8,1,2);Put(buf.data()+10,64,2);Put(buf.data()+12,192,4);Put(buf.data()+16,1704067200000ULL,8);buf[24]=std::byte{41};buf[25]=std::byte{3};buf[26]=std::byte{7};buf[27]=std::byte{12};buf[28]=std::byte{c.machine};std::size_t w=0;auto wr=os::WriteAt(rf,0,buf,w);if(!wr.Ok()||w!=buf.size()||!os::Flush(rf).Ok())return static_cast<std::int64_t>(SnowflakeError::kRegistryIoFailed); }
  else {std::uint64_t size=0;if(!os::FileSize(rf,size).Ok())return static_cast<std::int64_t>(SnowflakeError::kRegistryIoFailed);if(size!=192)return static_cast<std::int64_t>(SnowflakeError::kRegistryCorrupt);std::size_t n=0;auto rd=os::ReadAt(rf,0,buf,n);if(!rd.Ok()||n!=192)return static_cast<std::int64_t>(SnowflakeError::kRegistryIoFailed);
    if(!Eq(buf.data(),8,"BRSNFLK1"))return static_cast<std::int64_t>(SnowflakeError::kRegistryCorrupt);
    if(Get(buf.data()+8,2)!=1||Get(buf.data()+10,2)!=64||Get(buf.data()+12,4)!=192||Get(buf.data()+16,8)!=1704067200000ULL||buf[24]!=std::byte{41}||buf[25]!=std::byte{3}||buf[26]!=std::byte{7}||buf[27]!=std::byte{12})return static_cast<std::int64_t>(SnowflakeError::kRegistryIncompatible);
    if(std::to_integer<unsigned char>(buf[28])>7)return static_cast<std::int64_t>(SnowflakeError::kRegistryCorrupt);
    if(buf[28]!=std::byte{c.machine})return static_cast<std::int64_t>(SnowflakeError::kMachineIdMismatch);
    for (std::size_t i = 29; i < 64; ++i) {
      if (buf[i] != std::byte{0}) return static_cast<std::int64_t>(SnowflakeError::kRegistryCorrupt);
    }
    for (std::size_t i = 64; i < 192; ++i) {
      if (buf[i] != std::byte{0}) return static_cast<std::int64_t>(SnowflakeError::kRegistryCorrupt);
    }
  }
  // Registry validation reads the full immutable v1 metadata. Keep live ownership
  // locks strictly beyond its fixed 192-byte extent: Windows byte locks are
  // mandatory for ReadFile, while Linux OFD locks also support this range.
  for (std::uint8_t i = 0; i < 128; ++i) {
    const auto s = os::LockRange(rf, std::uint64_t{192} + i, 1, false);
    if (s.Ok()) {
      issuer.file = std::move(rf);
      issuer.slot = i;
      issuer.fence_initial = Now();
      os::UnlockRange(gatefile, 0, 1);
      gatefile.Close();
      return FinishFence();
    }
    // A slot held by another live issuer is skipped without waiting; any
    // other result is a lock failure.
    if (s.code != os::IoError::kBusy) {
      return static_cast<std::int64_t>(internal::snowflake::MapLockFailure(s));
    }
  }
  return static_cast<std::int64_t>(SnowflakeError::kSlotsExhausted);
}
}
std::int64_t Snowflake::Generate() noexcept {
  const auto& c=GetConfig(); if(c.error<0)return c.error;
  if(!issuer.ready){auto r=Register(c);if(r<0)return r;}
  return internal::snowflake::IssueAt(issuer.state,c.machine,issuer.slot,Now());
}
}
