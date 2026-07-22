/**
 * @file intrinsics.cc
 * @brief CPU 아키텍처·기능 런타임 감지 구현.
 *
 * x86은 CPUID와 XCR0(OS 확장 상태) 검증을, ARM은 플랫폼별
 * HWCAP/HWCAP2(Linux), sysctlbyname(macOS), IsProcessorFeaturePresent
 * (Windows)를 사용합니다. CpuFeature → {레지스터, 비트} 매핑은
 * kFeatureAndMask 테이블을 단일 진실 공급원으로 삼습니다.
 */
#include "common/intrinsics.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>

// Windows on ARM: IsProcessorFeaturePresent.
// <windows.h>는 매크로 오염이 심해 헤더가 아닌 여기서만 include합니다.
#if BEDROCK_ARCH_ARM && defined(_WIN32)
#include <windows.h>
#endif

namespace bedrock::intrinsic {

// ============================================================
// Feature 테이블
// ============================================================
// 기능 하나 = {CpuFeature 식별자, {Register::exx 인덱스, 비트 번호}}.
// 비트 번호는 Intel SDM Vol.2A / AMD APM Vol.3 / Linux hwcap.h 의
// 표기를 그대로 사용합니다. 예: AESNI = CPUID.01H:ECX[bit 25]
//                              → {CpuFeature::kAESNI, {kLeaf1Ecx, 25}}
struct FeatureMaskData {
  std::uint8_t reg;  // Register::exx[] 인덱스
  std::uint8_t bit;  // 레지스터 내 비트 번호 (0-31)
};

#if BEDROCK_ARCH_X86
// Register::exx[] 인덱스 (CPUID leaf/레지스터 → 저장 위치)
constexpr const std::uint8_t kLeaf1Ecx = 2;       // CPUID.01H:ECX
constexpr const std::uint8_t kLeaf1Edx = 3;       // CPUID.01H:EDX
constexpr const std::uint8_t kLeaf7Ebx = 5;       // CPUID.07H(sub0):EBX
constexpr const std::uint8_t kLeaf7Ecx = 6;       // CPUID.07H(sub0):ECX
constexpr const std::uint8_t kLeaf7Edx = 7;       // CPUID.07H(sub0):EDX
constexpr const std::uint8_t kExt1Ecx = 10;       // CPUID.80000001H:ECX
constexpr const std::uint8_t kExt1Edx = 11;       // CPUID.80000001H:EDX
constexpr const std::uint8_t kLeaf7Sub1Eax = 12;  // CPUID.07H(sub1):EAX
constexpr const std::uint8_t kExt8Ebx = 17;       // CPUID.80000008H:EBX
constexpr const std::uint8_t kExt21Eax = 20;      // CPUID.80000021H:EAX

// clang-format off
static const std::map<CpuFeature, FeatureMaskData> kFeatureAndMask = {
    // ---- kLeaf1Edx ----
    {CpuFeature::kFPU,                 {kLeaf1Edx,      0}},
    {CpuFeature::kVME,                 {kLeaf1Edx,      1}},
    {CpuFeature::kDE,                  {kLeaf1Edx,      2}},
    {CpuFeature::kPSE,                 {kLeaf1Edx,      3}},
    {CpuFeature::kTSC,                 {kLeaf1Edx,      4}},
    {CpuFeature::kMSR,                 {kLeaf1Edx,      5}},
    {CpuFeature::kPAE,                 {kLeaf1Edx,      6}},
    {CpuFeature::kMCE,                 {kLeaf1Edx,      7}},
    {CpuFeature::kCX8,                 {kLeaf1Edx,      8}},
    {CpuFeature::kAPIC,                {kLeaf1Edx,      9}},
    {CpuFeature::kSEP,                 {kLeaf1Edx,     11}},
    {CpuFeature::kMTRR,                {kLeaf1Edx,     12}},
    {CpuFeature::kPGE,                 {kLeaf1Edx,     13}},
    {CpuFeature::kMCA,                 {kLeaf1Edx,     14}},
    {CpuFeature::kCMOV,                {kLeaf1Edx,     15}},
    {CpuFeature::kPAT,                 {kLeaf1Edx,     16}},
    {CpuFeature::kPSE36,               {kLeaf1Edx,     17}},
    {CpuFeature::kPSN,                 {kLeaf1Edx,     18}},
    {CpuFeature::kCLFSH,               {kLeaf1Edx,     19}},
    {CpuFeature::kDS,                  {kLeaf1Edx,     21}},
    {CpuFeature::kACPI,                {kLeaf1Edx,     22}},
    {CpuFeature::kMMX,                 {kLeaf1Edx,     23}},
    {CpuFeature::kFXSR,                {kLeaf1Edx,     24}},
    {CpuFeature::kSSE,                 {kLeaf1Edx,     25}},
    {CpuFeature::kSSE2,                {kLeaf1Edx,     26}},
    {CpuFeature::kSS,                  {kLeaf1Edx,     27}},
    {CpuFeature::kHTT,                 {kLeaf1Edx,     28}},
    {CpuFeature::kTM,                  {kLeaf1Edx,     29}},
    {CpuFeature::kIA64,                {kLeaf1Edx,     30}},
    {CpuFeature::kPBE,                 {kLeaf1Edx,     31}},
    // ---- kLeaf1Ecx ----
    {CpuFeature::kSSE3,                {kLeaf1Ecx,      0}},
    {CpuFeature::kPCLMULQDQ,           {kLeaf1Ecx,      1}},
    {CpuFeature::kDTES64,              {kLeaf1Ecx,      2}},
    {CpuFeature::kMONITOR,             {kLeaf1Ecx,      3}},
    {CpuFeature::kDS_CPL,              {kLeaf1Ecx,      4}},
    {CpuFeature::kVMX,                 {kLeaf1Ecx,      5}},
    {CpuFeature::kSMX,                 {kLeaf1Ecx,      6}},
    {CpuFeature::kEST,                 {kLeaf1Ecx,      7}},
    {CpuFeature::kTM2,                 {kLeaf1Ecx,      8}},
    {CpuFeature::kSSSE3,               {kLeaf1Ecx,      9}},
    {CpuFeature::kCNXT_ID,             {kLeaf1Ecx,     10}},
    {CpuFeature::kSDBG,                {kLeaf1Ecx,     11}},
    {CpuFeature::kFMA,                 {kLeaf1Ecx,     12}},
    {CpuFeature::kCMPXCHG16B,          {kLeaf1Ecx,     13}},
    {CpuFeature::kXTPR,                {kLeaf1Ecx,     14}},
    {CpuFeature::kPDCM,                {kLeaf1Ecx,     15}},
    {CpuFeature::kPCID,                {kLeaf1Ecx,     17}},
    {CpuFeature::kDCA,                 {kLeaf1Ecx,     18}},
    {CpuFeature::kSSE4_1,              {kLeaf1Ecx,     19}},
    {CpuFeature::kSSE4_2,              {kLeaf1Ecx,     20}},
    {CpuFeature::kX2APIC,              {kLeaf1Ecx,     21}},
    {CpuFeature::kMOVBE,               {kLeaf1Ecx,     22}},
    {CpuFeature::kPOPCNT,              {kLeaf1Ecx,     23}},
    {CpuFeature::kTSC_DEADLINE,        {kLeaf1Ecx,     24}},
    {CpuFeature::kAESNI,               {kLeaf1Ecx,     25}},
    {CpuFeature::kXSAVE,               {kLeaf1Ecx,     26}},
    {CpuFeature::kOSXSAVE,             {kLeaf1Ecx,     27}},
    {CpuFeature::kAVX,                 {kLeaf1Ecx,     28}},
    {CpuFeature::kF16C,                {kLeaf1Ecx,     29}},
    {CpuFeature::kRDRAND,              {kLeaf1Ecx,     30}},
    {CpuFeature::kHYPERVISOR,          {kLeaf1Ecx,     31}},
    // ---- kLeaf7Ebx ----
    {CpuFeature::kBMI1,                {kLeaf7Ebx,      3}},
    {CpuFeature::kAVX2,                {kLeaf7Ebx,      5}},
    {CpuFeature::kBMI2,                {kLeaf7Ebx,      8}},
    {CpuFeature::kAVX512F,             {kLeaf7Ebx,     16}},
    {CpuFeature::kAVX512DQ,            {kLeaf7Ebx,     17}},
    {CpuFeature::kRDSEED,              {kLeaf7Ebx,     18}},
    {CpuFeature::kADX,                 {kLeaf7Ebx,     19}},
    {CpuFeature::kAVX512_IFMA,         {kLeaf7Ebx,     21}},
    {CpuFeature::kCLFLUSHOPT,          {kLeaf7Ebx,     23}},
    {CpuFeature::kCLWB,                {kLeaf7Ebx,     24}},
    {CpuFeature::kAVX512PF,            {kLeaf7Ebx,     26}},
    {CpuFeature::kAVX512ER,            {kLeaf7Ebx,     27}},
    {CpuFeature::kAVX512CD,            {kLeaf7Ebx,     28}},
    {CpuFeature::kSHA,                 {kLeaf7Ebx,     29}},
    {CpuFeature::kAVX512BW,            {kLeaf7Ebx,     30}},
    {CpuFeature::kAVX512VL,            {kLeaf7Ebx,     31}},
    // ---- kLeaf7Ecx ----
    {CpuFeature::kPREFETCHWT1,         {kLeaf7Ecx,      0}},
    {CpuFeature::kAVX512_VBMI,         {kLeaf7Ecx,      1}},
    {CpuFeature::kWAITPKG,             {kLeaf7Ecx,      5}},
    {CpuFeature::kAVX512_VBMI2,        {kLeaf7Ecx,      6}},
    {CpuFeature::kGFNI,                {kLeaf7Ecx,      8}},
    {CpuFeature::kVAES,                {kLeaf7Ecx,      9}},
    {CpuFeature::kVPCLMULQDQ,          {kLeaf7Ecx,     10}},
    {CpuFeature::kAVX512_VNNI,         {kLeaf7Ecx,     11}},
    {CpuFeature::kAVX512_BITALG,       {kLeaf7Ecx,     12}},
    {CpuFeature::kAVX512_VPOPCNTDQ,    {kLeaf7Ecx,     14}},
    {CpuFeature::kRDPID,               {kLeaf7Ecx,     22}},
    {CpuFeature::kKL,                  {kLeaf7Ecx,     23}},
    {CpuFeature::kCLDEMOTE,            {kLeaf7Ecx,     25}},
    {CpuFeature::kMOVDIRI,             {kLeaf7Ecx,     27}},
    {CpuFeature::kMOVDIR64B,           {kLeaf7Ecx,     28}},
    {CpuFeature::kENQCMD,              {kLeaf7Ecx,     29}},
    // ---- kLeaf7Edx ----
    {CpuFeature::kAVX512_4VNNIW,       {kLeaf7Edx,      2}},
    {CpuFeature::kAVX512_4FMAPS,       {kLeaf7Edx,      3}},
    {CpuFeature::kFSRM,                {kLeaf7Edx,      4}},
    {CpuFeature::kUINTR,               {kLeaf7Edx,      5}},
    {CpuFeature::kAVX512_VP2INTERSECT, {kLeaf7Edx,      8}},
    {CpuFeature::kSERIALIZE,           {kLeaf7Edx,     14}},
    {CpuFeature::kAMX_BF16,            {kLeaf7Edx,     22}},
    {CpuFeature::kAVX512_FP16,         {kLeaf7Edx,     23}},
    {CpuFeature::kAMX_TILE,            {kLeaf7Edx,     24}},
    {CpuFeature::kAMX_INT8,            {kLeaf7Edx,     25}},
    // ---- kExt1Ecx ----
    {CpuFeature::kLAHF_LM,             {kExt1Ecx,       0}},
    {CpuFeature::kLZCNT,               {kExt1Ecx,       5}},
    {CpuFeature::kSSE4A,               {kExt1Ecx,       6}},
    {CpuFeature::kPREFETCHW,           {kExt1Ecx,       8}},
    {CpuFeature::kXOP,                 {kExt1Ecx,      11}},
    {CpuFeature::kFMA4,                {kExt1Ecx,      16}},
    {CpuFeature::kTBM,                 {kExt1Ecx,      21}},
    // ---- kExt1Edx ----
    {CpuFeature::kSYSCALL,             {kExt1Edx,      11}},
    {CpuFeature::kMMXEXT,              {kExt1Edx,      22}},
    {CpuFeature::kRDTSCP,              {kExt1Edx,      27}},
    {CpuFeature::kLM,                  {kExt1Edx,      29}},
    {CpuFeature::k3DNOWEXT,            {kExt1Edx,      30}},
    {CpuFeature::k3DNOW,               {kExt1Edx,      31}},
    // ---- kLeaf7Sub1Eax ----
    {CpuFeature::kSHA512,              {kLeaf7Sub1Eax,  0}},
    {CpuFeature::kSM3,                 {kLeaf7Sub1Eax,  1}},
    {CpuFeature::kSM4,                 {kLeaf7Sub1Eax,  2}},
    {CpuFeature::kAVX_VNNI,            {kLeaf7Sub1Eax,  4}},
    {CpuFeature::kAVX512_BF16,         {kLeaf7Sub1Eax,  5}},
    {CpuFeature::kCMPCCXADD,           {kLeaf7Sub1Eax,  7}},
    {CpuFeature::kAMX_FP16,            {kLeaf7Sub1Eax, 21}},
    {CpuFeature::kAVX_IFMA,            {kLeaf7Sub1Eax, 23}},
    {CpuFeature::kMOVRS,               {kLeaf7Sub1Eax, 31}},
    // ---- kExt8Ebx ----
    {CpuFeature::kCLZERO,              {kExt8Ebx,       0}},
    {CpuFeature::kRDPRU,               {kExt8Ebx,       4}},
    {CpuFeature::kWBNOINVD,            {kExt8Ebx,       9}},
    // ---- kExt21Eax ----
    {CpuFeature::kPREFETCHI,           {kExt21Eax,     20}},
    {CpuFeature::kAVX512_BMM,          {kExt21Eax,     23}},
};
// clang-format on

// ============================================================
// OS 상태 검증 (XCR0)
// ============================================================
// AVX 이상은 CPU 지원(CPUID)과 별개로, OS가 해당 확장 레지스터 상태를
// 컨텍스트 스위치 시 저장·복원하도록 활성화했는지(XCR0)를 확인해야
// 합니다. 확인 없이 쓰면 구형 OS/일부 하이퍼바이저에서 CPUID는 지원을
// 보고하는데 실행 시 #UD 로 죽습니다. 요구 상태는 계열별로 다릅니다:
//   YMM  (XCR0[2:1])         : AVX 계열 (VEX 인코딩)
//   ZMM  (XCR0[7:5] + [2:1]) : AVX-512 계열 (EVEX 인코딩)
//   TILE (XCR0[18:17])       : AMX 계열
// 아래 map에 없는 기능은 OS 상태 요구가 없습니다. (XMM까지는 XSAVE
// 이전 메커니즘(FXSR)으로 동작하므로 검증 대상이 아님)
constexpr const std::uint64_t kXcr0Ymm = 0x6;       // XCR0[2:1]
constexpr const std::uint64_t kXcr0Zmm = 0xE6;      // XCR0[7:5]+[2:1]
constexpr const std::uint64_t kXcr0Tile = 0x60000;  // XCR0[18:17]

// clang-format off
static const std::map<CpuFeature, std::uint64_t> kRequiredXcr0 = {
    // ---- AVX 계열 (YMM) ----
    {CpuFeature::kAVX,                 kXcr0Ymm},
    {CpuFeature::kAVX2,                kXcr0Ymm},
    {CpuFeature::kFMA,                 kXcr0Ymm},
    {CpuFeature::kF16C,                kXcr0Ymm},
    {CpuFeature::kVAES,                kXcr0Ymm},
    {CpuFeature::kVPCLMULQDQ,          kXcr0Ymm},
    {CpuFeature::kAVX_VNNI,            kXcr0Ymm},
    {CpuFeature::kAVX_IFMA,            kXcr0Ymm},
    {CpuFeature::kXOP,                 kXcr0Ymm},
    {CpuFeature::kFMA4,                kXcr0Ymm},
    {CpuFeature::kSHA512,              kXcr0Ymm},  // x86: VEX(YMM) 인코딩
    {CpuFeature::kSM3,                 kXcr0Ymm},
    {CpuFeature::kSM4,                 kXcr0Ymm},
    // ---- AVX-512 계열 (ZMM) ----
    {CpuFeature::kAVX512F,             kXcr0Zmm},
    {CpuFeature::kAVX512DQ,            kXcr0Zmm},
    {CpuFeature::kAVX512_IFMA,         kXcr0Zmm},
    {CpuFeature::kAVX512PF,            kXcr0Zmm},
    {CpuFeature::kAVX512ER,            kXcr0Zmm},
    {CpuFeature::kAVX512CD,            kXcr0Zmm},
    {CpuFeature::kAVX512BW,            kXcr0Zmm},
    {CpuFeature::kAVX512VL,            kXcr0Zmm},
    {CpuFeature::kAVX512_VBMI,         kXcr0Zmm},
    {CpuFeature::kAVX512_VBMI2,        kXcr0Zmm},
    {CpuFeature::kAVX512_VNNI,         kXcr0Zmm},
    {CpuFeature::kAVX512_BITALG,       kXcr0Zmm},
    {CpuFeature::kAVX512_VPOPCNTDQ,    kXcr0Zmm},
    {CpuFeature::kAVX512_4VNNIW,       kXcr0Zmm},
    {CpuFeature::kAVX512_4FMAPS,       kXcr0Zmm},
    {CpuFeature::kAVX512_VP2INTERSECT, kXcr0Zmm},
    {CpuFeature::kAVX512_FP16,         kXcr0Zmm},
    {CpuFeature::kAVX512_BF16,         kXcr0Zmm},
    {CpuFeature::kAVX512_BMM,          kXcr0Zmm},
    // ---- AMX 계열 (TILE) ----
    {CpuFeature::kAMX_TILE,            kXcr0Tile},
    {CpuFeature::kAMX_BF16,            kXcr0Tile},
    {CpuFeature::kAMX_INT8,            kXcr0Tile},
    {CpuFeature::kAMX_FP16,            kXcr0Tile},
};
// clang-format on

/**
 * @brief XCR0(확장 제어 레지스터 0)을 읽습니다.
 *
 * XGETBV 는 CPUID.OSXSAVE=1 일 때만 유효하므로 호출 측이 kOSXSAVE
 * 를 먼저 확인해야 합니다. 값은 프로세스 수명 동안 사실상 불변이므로
 * 1회 읽어 캐시합니다.
 *
 * @return 현재 XCR0 값(하위 32비트=EAX, 상위 32비트=EDX).
 */
static std::uint64_t ReadXcr0() {
#ifdef _WIN32
  static const std::uint64_t xcr0 = _xgetbv(0);
#else
  // gcc/clang 의 _xgetbv 인트린식은 -mxsave 플래그를 요구하므로,
  // 플래그 의존이 없는 인라인 어셈블리를 사용합니다.
  static const std::uint64_t xcr0 = [] {
    std::uint32_t eax = 0;
    std::uint32_t edx = 0;
    __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0u));
    return (static_cast<std::uint64_t>(edx) << 32) | eax;
  }();
#endif
  return xcr0;
}
#elif BEDROCK_ARCH_ARM
// Register::exx[] 인덱스 (HWCAP → 저장 위치. 상위 32비트는 exx[1]/exx[3])
constexpr const std::uint8_t kHwcapLo = 0;   // HWCAP  하위 32비트
constexpr const std::uint8_t kHwcap2Lo = 2;  // HWCAP2 하위 32비트

// 이름/비트는 커널 HWCAP 명칭·비트 위치(arch/arm64/include/uapi/asm/hwcap.h)를
// 그대로 따릅니다(예: ASIMD = NEON, PMULL = polynomial multiply). macOS 경로는
// sysctl 결과를 같은 비트 위치에 채워 넣어 같은 테이블/이름을 재사용합니다.
// clang-format off
static const std::map<CpuFeature, FeatureMaskData> kFeatureAndMask = {
    // ---- kHwcapLo ----
    {CpuFeature::kFP,         {kHwcapLo,   0}},
    {CpuFeature::kASIMD,      {kHwcapLo,   1}},
    {CpuFeature::kEVTSTRM,    {kHwcapLo,   2}},
    {CpuFeature::kAES,        {kHwcapLo,   3}},
    {CpuFeature::kPMULL,      {kHwcapLo,   4}},
    {CpuFeature::kSHA1,       {kHwcapLo,   5}},
    {CpuFeature::kSHA2,       {kHwcapLo,   6}},
    {CpuFeature::kCRC32,      {kHwcapLo,   7}},
    {CpuFeature::kATOMICS,    {kHwcapLo,   8}},
    {CpuFeature::kFPHP,       {kHwcapLo,   9}},
    {CpuFeature::kASIMDHP,    {kHwcapLo,  10}},
    {CpuFeature::kCPUID,      {kHwcapLo,  11}},
    {CpuFeature::kASIMDRDM,   {kHwcapLo,  12}},
    {CpuFeature::kJSCVT,      {kHwcapLo,  13}},
    {CpuFeature::kFCMA,       {kHwcapLo,  14}},
    {CpuFeature::kLRCPC,      {kHwcapLo,  15}},
    {CpuFeature::kDCPOP,      {kHwcapLo,  16}},
    {CpuFeature::kSHA3,       {kHwcapLo,  17}},
    {CpuFeature::kSM3,        {kHwcapLo,  18}},
    {CpuFeature::kSM4,        {kHwcapLo,  19}},
    {CpuFeature::kASIMDDP,    {kHwcapLo,  20}},
    {CpuFeature::kSHA512,     {kHwcapLo,  21}},
    {CpuFeature::kSVE,        {kHwcapLo,  22}},
    {CpuFeature::kASIMDFHM,   {kHwcapLo,  23}},
    {CpuFeature::kDIT,        {kHwcapLo,  24}},
    {CpuFeature::kUSCAT,      {kHwcapLo,  25}},
    {CpuFeature::kILRCPC,     {kHwcapLo,  26}},
    {CpuFeature::kFLAGM,      {kHwcapLo,  27}},
    {CpuFeature::kSSBS,       {kHwcapLo,  28}},
    {CpuFeature::kSB,         {kHwcapLo,  29}},
    {CpuFeature::kPACA,       {kHwcapLo,  30}},
    {CpuFeature::kPACG,       {kHwcapLo,  31}},
    // ---- kHwcap2Lo ----
    {CpuFeature::kDCPODP,     {kHwcap2Lo,  0}},
    {CpuFeature::kSVE2,       {kHwcap2Lo,  1}},
    {CpuFeature::kSVEAES,     {kHwcap2Lo,  2}},
    {CpuFeature::kSVEPMULL,   {kHwcap2Lo,  3}},
    {CpuFeature::kSVEBITPERM, {kHwcap2Lo,  4}},
    {CpuFeature::kSVESHA3,    {kHwcap2Lo,  5}},
    {CpuFeature::kSVESM4,     {kHwcap2Lo,  6}},
    {CpuFeature::kFLAGM2,     {kHwcap2Lo,  7}},
    {CpuFeature::kFRINT,      {kHwcap2Lo,  8}},
    {CpuFeature::kSVEI8MM,    {kHwcap2Lo,  9}},
    {CpuFeature::kSVEF32MM,   {kHwcap2Lo, 10}},
    {CpuFeature::kSVEF64MM,   {kHwcap2Lo, 11}},
    {CpuFeature::kSVEBF16,    {kHwcap2Lo, 12}},
    {CpuFeature::kI8MM,       {kHwcap2Lo, 13}},
    {CpuFeature::kBF16,       {kHwcap2Lo, 14}},
    {CpuFeature::kDGH,        {kHwcap2Lo, 15}},
    {CpuFeature::kRNG,        {kHwcap2Lo, 16}},
    {CpuFeature::kBTI,        {kHwcap2Lo, 17}},
    {CpuFeature::kMTE,        {kHwcap2Lo, 18}},
    {CpuFeature::kECV,        {kHwcap2Lo, 19}},
    {CpuFeature::kAFP,        {kHwcap2Lo, 20}},
    {CpuFeature::kRPRES,      {kHwcap2Lo, 21}},
    {CpuFeature::kMTE3,       {kHwcap2Lo, 22}},
    {CpuFeature::kSME,        {kHwcap2Lo, 23}},
    {CpuFeature::kSME_I16I64, {kHwcap2Lo, 24}},
    {CpuFeature::kSME_F64F64, {kHwcap2Lo, 25}},
    {CpuFeature::kSME_I8I32,  {kHwcap2Lo, 26}},
    {CpuFeature::kSME_F16F32, {kHwcap2Lo, 27}},
    {CpuFeature::kSME_B16F32, {kHwcap2Lo, 28}},
    {CpuFeature::kSME_F32F32, {kHwcap2Lo, 29}},
    {CpuFeature::kSME_FA64,   {kHwcap2Lo, 30}},
    {CpuFeature::kWFXT,       {kHwcap2Lo, 31}},
};
// clang-format on
#else
// 미지원 아키텍처: 빈 테이블 (모든 기능 미지원)
static const std::map<CpuFeature, FeatureMaskData> kFeatureAndMask = {};
#endif

#if BEDROCK_ARCH_ARM && (defined(__APPLE__) || defined(_WIN32))
/**
 * @brief 테이블에서 (레지스터, 기능)에 해당하는 비트 마스크를 얻습니다.
 *
 * kFeatureAndMask 테이블이 단일 진실 공급원(single source of truth)이
 * 되도록, 기능→비트 매핑이 필요한 내부 코드는 이 헬퍼를 사용합니다.
 * 이 파일 내부 전용 헬퍼이므로 static(내부 링크)으로 둡니다.
 *
 * @param reg 대상 Register::exx[] 인덱스.
 * @param feature 비트를 조회할 기능 식별자.
 * @return 해당 기능의 비트 마스크. reg 가 일치하지 않거나 테이블에
 *         없으면 0.
 */
static std::uint32_t FeatureBit(std::uint8_t reg, CpuFeature feature) {
  const auto it = kFeatureAndMask.find(feature);
  if (it != kFeatureAndMask.end() && it->second.reg == reg) {
    return 1u << it->second.bit;
  }
  return 0u;
}
#endif

/**
 * @brief x86/x64 에서 CPUID 로 CPU 기능 레지스터를 조회합니다.
 *
 * leaf 1, leaf 7(서브리프 0/1), 확장 leaf 0x80000001/0x80000008/
 * 0x80000021 조회 결과를 Register::exx 에 채웁니다. BEDROCK_ARCH_X86
 * 이 아니면 0으로 초기화된 Register 를 그대로 반환합니다.
 *
 * @return CPUID 결과가 채워진(또는 미지원 시 0인) Register.
 */
static Register GetX86CpuFeatures() {
  Register features = {};
#if BEDROCK_ARCH_X86
#ifndef _WIN32
  // leaf 1, leaf 7(sub0), 확장 leaf 0x80000001. __get_cpuid* 는 지원 leaf를
  // 내부 검사하여 미지원 시 아무것도 쓰지 않음(0 유지).
  __get_cpuid(1u, &features.exx[0], &features.exx[1], &features.exx[2],
              &features.exx[3]);
  __get_cpuid_count(7u, 0u, &features.exx[4], &features.exx[5],
                    &features.exx[6], &features.exx[7]);
  __get_cpuid(0x80000001u, &features.exx[8], &features.exx[9],
              &features.exx[10], &features.exx[11]);
  __get_cpuid_count(7u, 1u, &features.exx[12], &features.exx[13],
                    &features.exx[14], &features.exx[15]);
  __get_cpuid(0x80000008u, &features.exx[16], &features.exx[17],
              &features.exx[18], &features.exx[19]);
  __get_cpuid(0x80000021u, &features.exx[20], &features.exx[21],
              &features.exx[22], &features.exx[23]);
#else
  int regs[4] = {0, 0, 0, 0};
  __cpuid(regs, 0);
  const unsigned int max_basic = static_cast<unsigned int>(regs[0]);
  __cpuid(reinterpret_cast<int*>(&features.exx[0]), 1);
  if (max_basic >= 7u) {
    __cpuidex(reinterpret_cast<int*>(&features.exx[4]), 7, 0);
  }
  __cpuid(regs, static_cast<int>(0x80000000u));
  const unsigned int max_ext = static_cast<unsigned int>(regs[0]);
  if (max_ext >= 0x80000001u) {
    __cpuid(reinterpret_cast<int*>(&features.exx[8]),
            static_cast<int>(0x80000001u));
  }
  if (max_ext >= 0x80000008u) {
    __cpuid(reinterpret_cast<int*>(&features.exx[16]),
            static_cast<int>(0x80000008u));
  }
  if (max_ext >= 0x80000021u) {
    __cpuid(reinterpret_cast<int*>(&features.exx[20]),
            static_cast<int>(0x80000021u));
  }
  if (max_basic >= 7u) {
    __cpuidex(reinterpret_cast<int*>(&features.exx[12]), 7, 1);
  }
#endif
#endif
  return features;
}

/**
 * @brief ARM 에서 플랫폼별 방법으로 CPU 기능 레지스터를 조회합니다.
 *
 * Linux 는 getauxval(AT_HWCAP/AT_HWCAP2), macOS(애플 실리콘)는
 * sysctlbyname("hw.optional.arm.FEAT_*"), Windows(ARM)는
 * IsProcessorFeaturePresent(PF_ARM_*) 결과를 HWCAP/HWCAP2 와 같은
 * 비트 위치에 채웁니다. BEDROCK_ARCH_ARM 이 아니거나 위 플랫폼에
 * 해당하지 않으면 0으로 초기화된 Register 를 그대로 반환합니다.
 *
 * @return 플랫폼별 조회 결과가 채워진(또는 미지원 시 0인) Register.
 */
static Register GetARMCpuFeatures() {
  Register features = {};
#if BEDROCK_ARCH_ARM
#if defined(__aarch64__) && defined(__linux__)
  const unsigned long hwcap = getauxval(AT_HWCAP);
  features.exx[0] = static_cast<std::uint32_t>(hwcap & 0xffffffffUL);
  features.exx[1] = static_cast<std::uint32_t>((hwcap >> 32) & 0xffffffffUL);
#ifdef AT_HWCAP2
  const unsigned long hwcap2 = getauxval(AT_HWCAP2);
  features.exx[2] = static_cast<std::uint32_t>(hwcap2 & 0xffffffffUL);
  features.exx[3] = static_cast<std::uint32_t>((hwcap2 >> 32) & 0xffffffffUL);
#endif
#elif defined(__aarch64__) && defined(__APPLE__)
  // 애플 실리콘: sysctlbyname 으로 ARM 기능을 조회해 같은 HWCAP 비트에
  // 채웁니다. (키 이름은 Apple 문서 기준. 실기에서 한 번 검증 권장. SVE/SME 은
  // 미지원=0.)
  auto has = [](const char* name) -> bool {
    int value = 0;
    std::size_t size = sizeof(value);
    return sysctlbyname(name, &value, &size, nullptr, 0) == 0 && value != 0;
  };
  // 항상 존재
  std::uint32_t h = FeatureBit(kHwcapLo, CpuFeature::kFP) |
                    FeatureBit(kHwcapLo, CpuFeature::kASIMD);
  if (has("hw.optional.arm.FEAT_AES"))
    h |= FeatureBit(kHwcapLo, CpuFeature::kAES);
  if (has("hw.optional.arm.FEAT_PMULL"))
    h |= FeatureBit(kHwcapLo, CpuFeature::kPMULL);
  if (has("hw.optional.arm.FEAT_SHA1"))
    h |= FeatureBit(kHwcapLo, CpuFeature::kSHA1);
  if (has("hw.optional.arm.FEAT_SHA256"))
    h |= FeatureBit(kHwcapLo, CpuFeature::kSHA2);
  if (has("hw.optional.arm.FEAT_SHA512"))
    h |= FeatureBit(kHwcapLo, CpuFeature::kSHA512);
  if (has("hw.optional.arm.FEAT_SHA3"))
    h |= FeatureBit(kHwcapLo, CpuFeature::kSHA3);
  if (has("hw.optional.arm.FEAT_CRC32"))
    h |= FeatureBit(kHwcapLo, CpuFeature::kCRC32);
  if (has("hw.optional.arm.FEAT_LSE"))
    h |= FeatureBit(kHwcapLo, CpuFeature::kATOMICS);
  if (has("hw.optional.arm.FEAT_FP16")) {
    h |= FeatureBit(kHwcapLo, CpuFeature::kFPHP) |
         FeatureBit(kHwcapLo, CpuFeature::kASIMDHP);
  }
  if (has("hw.optional.arm.FEAT_DotProd"))
    h |= FeatureBit(kHwcapLo, CpuFeature::kASIMDDP);
  if (has("hw.optional.arm.FEAT_FHM"))
    h |= FeatureBit(kHwcapLo, CpuFeature::kASIMDFHM);
  if (has("hw.optional.arm.FEAT_RDM"))
    h |= FeatureBit(kHwcapLo, CpuFeature::kASIMDRDM);
  if (has("hw.optional.arm.FEAT_FCMA"))
    h |= FeatureBit(kHwcapLo, CpuFeature::kFCMA);
  if (has("hw.optional.arm.FEAT_JSCVT"))
    h |= FeatureBit(kHwcapLo, CpuFeature::kJSCVT);
  if (has("hw.optional.arm.FEAT_LRCPC"))
    h |= FeatureBit(kHwcapLo, CpuFeature::kLRCPC);
  if (has("hw.optional.arm.FEAT_DPB"))
    h |= FeatureBit(kHwcapLo, CpuFeature::kDCPOP);
  if (has("hw.optional.arm.FEAT_DIT"))
    h |= FeatureBit(kHwcapLo, CpuFeature::kDIT);
  if (has("hw.optional.arm.FEAT_SB"))
    h |= FeatureBit(kHwcapLo, CpuFeature::kSB);
  if (has("hw.optional.arm.FEAT_SSBS"))
    h |= FeatureBit(kHwcapLo, CpuFeature::kSSBS);
  if (has("hw.optional.arm.FEAT_FlagM"))
    h |= FeatureBit(kHwcapLo, CpuFeature::kFLAGM);
  if (has("hw.optional.arm.FEAT_PAuth"))
    h |= FeatureBit(kHwcapLo, CpuFeature::kPACA);
  features.exx[0] = h;

  std::uint32_t h2 = 0u;
  if (has("hw.optional.arm.FEAT_BF16"))
    h2 |= FeatureBit(kHwcap2Lo, CpuFeature::kBF16);
  if (has("hw.optional.arm.FEAT_I8MM"))
    h2 |= FeatureBit(kHwcap2Lo, CpuFeature::kI8MM);
  if (has("hw.optional.arm.FEAT_FlagM2"))
    h2 |= FeatureBit(kHwcap2Lo, CpuFeature::kFLAGM2);
  if (has("hw.optional.arm.FEAT_FRINTTS"))
    h2 |= FeatureBit(kHwcap2Lo, CpuFeature::kFRINT);
  features.exx[2] = h2;
#elif defined(_WIN32)
  // Windows on ARM: IsProcessorFeaturePresent 로 조회해 Linux HWCAP과 같은
  // 비트 위치에 채웁니다. PF_* 매크로는 SDK 버전에 따라 없을 수 있어
  // 문서화된 숫자 값을 직접 사용합니다. OS가 모르는 값은 FALSE 를
  // 반환하므로(감지 불가 = 미지원) 구형 Windows에서도 안전합니다.
  // https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-isprocessorfeaturepresent
  struct PfMap {
    unsigned long pf;    // IsProcessorFeaturePresent 인자 (PF_*)
    std::uint8_t reg;    // Register::exx[] 인덱스
    CpuFeature feature;  // 대응 기능
  };
  // clang-format off
  static constexpr PfMap kWinPfMap[] = {
      {30, kHwcapLo,  CpuFeature::kAES},         // PF_ARM_V8_CRYPTO (AES/PMULL/SHA1/SHA2 통합 플래그)
      {30, kHwcapLo,  CpuFeature::kPMULL},       // PF_ARM_V8_CRYPTO
      {30, kHwcapLo,  CpuFeature::kSHA1},        // PF_ARM_V8_CRYPTO
      {30, kHwcapLo,  CpuFeature::kSHA2},        // PF_ARM_V8_CRYPTO
      {31, kHwcapLo,  CpuFeature::kCRC32},       // PF_ARM_V8_CRC32
      {34, kHwcapLo,  CpuFeature::kATOMICS},     // PF_ARM_V81_ATOMIC
      {43, kHwcapLo,  CpuFeature::kASIMDDP},     // PF_ARM_V82_DP
      {44, kHwcapLo,  CpuFeature::kJSCVT},       // PF_ARM_V83_JSCVT
      {45, kHwcapLo,  CpuFeature::kLRCPC},       // PF_ARM_V83_LRCPC
      {46, kHwcapLo,  CpuFeature::kSVE},         // PF_ARM_SVE
      {62, kHwcapLo,  CpuFeature::kUSCAT},       // PF_ARM_LSE2 (FEAT_LSE2)
      {64, kHwcapLo,  CpuFeature::kSHA3},        // PF_ARM_SHA3
      {65, kHwcapLo,  CpuFeature::kSHA512},      // PF_ARM_SHA512
      {67, kHwcapLo,  CpuFeature::kFPHP},        // PF_ARM_V82_FP16
      {67, kHwcapLo,  CpuFeature::kASIMDHP},     // PF_ARM_V82_FP16
      {47, kHwcap2Lo, CpuFeature::kSVE2},        // PF_ARM_SVE2
      {49, kHwcap2Lo, CpuFeature::kSVEAES},      // PF_ARM_SVE_AES
      {50, kHwcap2Lo, CpuFeature::kSVEPMULL},    // PF_ARM_SVE_PMULL128
      {51, kHwcap2Lo, CpuFeature::kSVEBITPERM},  // PF_ARM_SVE_BITPERM
      {52, kHwcap2Lo, CpuFeature::kSVEBF16},     // PF_ARM_SVE_BF16
      {55, kHwcap2Lo, CpuFeature::kSVESHA3},     // PF_ARM_SVE_SHA3
      {56, kHwcap2Lo, CpuFeature::kSVESM4},      // PF_ARM_SVE_SM4
      {57, kHwcap2Lo, CpuFeature::kSVEI8MM},     // PF_ARM_SVE_I8MM
      {58, kHwcap2Lo, CpuFeature::kSVEF32MM},    // PF_ARM_SVE_F32MM
      {59, kHwcap2Lo, CpuFeature::kSVEF64MM},    // PF_ARM_SVE_F64MM
      {66, kHwcap2Lo, CpuFeature::kI8MM},        // PF_ARM_V82_I8MM
      {68, kHwcap2Lo, CpuFeature::kBF16},        // PF_ARM_V86_BF16
      {70, kHwcap2Lo, CpuFeature::kSME},         // PF_ARM_SME
      {85, kHwcap2Lo, CpuFeature::kSME_F64F64},  // PF_ARM_SME_F64F64
      {86, kHwcap2Lo, CpuFeature::kSME_I16I64},  // PF_ARM_SME_I16I64
      {88, kHwcap2Lo, CpuFeature::kSME_FA64},    // PF_ARM_SME_FA64
  };
  // clang-format on
  // ARMv8-A 기본 기능: FP/ASIMD 는 항상 존재
  features.exx[kHwcapLo] = FeatureBit(kHwcapLo, CpuFeature::kFP) |
                           FeatureBit(kHwcapLo, CpuFeature::kASIMD);
  for (const auto& m : kWinPfMap) {
    if (IsProcessorFeaturePresent(m.pf) != 0) {
      features.exx[m.reg] |= FeatureBit(m.reg, m.feature);
    }
  }
#endif
#endif
  return features;
}

// CPU가 지원하는 명령어셋을 런타임에 감지합니다.
//  x86/x64 : CPUID(leaf 1)의 EAX/EBX/ECX/EDX 를 exx[0..3] 에 담습니다.
//  ARM     : HWCAP/HWCAP2(macOS sysctl, Windows IsProcessorFeaturePresent)
//            결과를 위 비트 정의대로 담습니다.
//  그 외   : 0(모든 기능 미지원).
Register GetCPUFeatures() {
  Register features = {};
#if BEDROCK_ARCH_X86
  return GetX86CpuFeatures();
#elif BEDROCK_ARCH_ARM
  return GetARMCpuFeatures();
#endif
}

bool HasFeature(const Register& cpu_feature_flag, CpuFeature feature) {
  const auto mask = kFeatureAndMask.find(feature);
  if (mask == kFeatureAndMask.end()) {
    return false;
  }
  if ((cpu_feature_flag.exx[mask->second.reg] & (1u << mask->second.bit)) ==
      0x0u) {
    return false;
  }
#if BEDROCK_ARCH_X86
  // AVX 이상(YMM/ZMM/TILE 상태를 쓰는 기능)은 OS가 해당 상태를
  // 활성화했는지(XCR0)까지 항상 추가 검증합니다.
  const auto required = kRequiredXcr0.find(feature);
  if (required != kRequiredXcr0.end()) {
    if (!HasFeature(cpu_feature_flag, CpuFeature::kOSXSAVE)) {
      return false;
    }
    if ((ReadXcr0() & required->second) != required->second) {
      return false;
    }
  }
#endif
  return true;
}

};  // namespace bedrock::intrinsic
