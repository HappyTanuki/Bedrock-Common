#include "common/intrinsics.h"

#include <array>
#include <cstddef>

// ARM의 런타임 기능 인식은 CPUID가 아니라 OS가 제공하는 정보로 합니다.
//   Linux : getauxval(AT_HWCAP/AT_HWCAP2)
//   macOS : sysctlbyname("hw.optional.arm.FEAT_*")  (애플 실리콘)
//   그 외(Windows on ARM 등): 미구현 → 0
#if BEDROCK_ARCH_ARM
#if defined(__linux__)
#include <sys/auxv.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#endif

namespace bedrock::intrinsic {

#if BEDROCK_ARCH_ARM
// AArch64 Linux HWCAP/HWCAP2 비트(arch/arm64/include/uapi/asm/hwcap.h, 커널 ABI).
// 이 비트 위치는 Linux/macOS 공용 "정규 표현"으로 쓰입니다. macOS 경로는
// sysctl 결과를 아래 동일 비트 위치에 채워 넣어 같은 테이블/이름을 재사용합니다.
//   HWCAP  → exx[0](하위32) / exx[1](상위32)
//   HWCAP2 → exx[2](하위32) / exx[3](상위32)
enum ArmHwcapBits : std::uint32_t {
  kFP = 1u << 0,        kASIMD = 1u << 1,     kEVTSTRM = 1u << 2,
  kAES = 1u << 3,       kPMULL = 1u << 4,     kSHA1 = 1u << 5,
  kSHA2 = 1u << 6,      kCRC32 = 1u << 7,     kATOMICS = 1u << 8,
  kFPHP = 1u << 9,      kASIMDHP = 1u << 10,  kCPUID = 1u << 11,
  kASIMDRDM = 1u << 12, kJSCVT = 1u << 13,    kFCMA = 1u << 14,
  kLRCPC = 1u << 15,    kDCPOP = 1u << 16,    kSHA3 = 1u << 17,
  kSM3 = 1u << 18,      kSM4 = 1u << 19,      kASIMDDP = 1u << 20,
  kSHA512 = 1u << 21,   kSVE = 1u << 22,      kASIMDFHM = 1u << 23,
  kDIT = 1u << 24,      kUSCAT = 1u << 25,    kILRCPC = 1u << 26,
  kFLAGM = 1u << 27,    kSSBS = 1u << 28,     kSB = 1u << 29,
  kPACA = 1u << 30,     kPACG = 1u << 31
};
enum ArmHwcap2Bits : std::uint32_t {
  k2_DCPODP = 1u << 0,     k2_SVE2 = 1u << 1,        k2_SVEAES = 1u << 2,
  k2_SVEPMULL = 1u << 3,   k2_SVEBITPERM = 1u << 4,  k2_SVESHA3 = 1u << 5,
  k2_SVESM4 = 1u << 6,     k2_FLAGM2 = 1u << 7,      k2_FRINT = 1u << 8,
  k2_SVEI8MM = 1u << 9,    k2_SVEF32MM = 1u << 10,   k2_SVEF64MM = 1u << 11,
  k2_SVEBF16 = 1u << 12,   k2_I8MM = 1u << 13,       k2_BF16 = 1u << 14,
  k2_DGH = 1u << 15,       k2_RNG = 1u << 16,        k2_BTI = 1u << 17,
  k2_MTE = 1u << 18,       k2_ECV = 1u << 19,        k2_AFP = 1u << 20,
  k2_RPRES = 1u << 21,     k2_MTE3 = 1u << 22,       k2_SME = 1u << 23,
  k2_SME_I16I64 = 1u << 24, k2_SME_F64F64 = 1u << 25, k2_SME_I8I32 = 1u << 26,
  k2_SME_F16F32 = 1u << 27, k2_SME_B16F32 = 1u << 28, k2_SME_F32F32 = 1u << 29,
  k2_SME_FA64 = 1u << 30,   k2_WFXT = 1u << 31
};
#endif  // BEDROCK_ARCH_ARM

// CPU가 지원하는 명령어셋을 런타임에 감지합니다.
//  x86/x64 : CPUID(leaf 1)의 EAX/EBX/ECX/EDX 를 exx[0..3] 에 담습니다.
//  ARM     : HWCAP/HWCAP2(또는 macOS sysctl) 결과를 위 비트 정의대로 담습니다.
//  그 외   : 0(모든 기능 미지원).
Register GetCPUFeatures() {
  Register features = {};
#if BEDROCK_ARCH_X86
#ifndef _WIN32
  // leaf 1, leaf 7(sub0), 확장 leaf 0x80000001. __get_cpuid* 는 지원 leaf를
  // 내부 검사하여 미지원 시 아무것도 쓰지 않음(0 유지).
  __get_cpuid(1u, &features.exx[0], &features.exx[1], &features.exx[2],
              &features.exx[3]);
  __get_cpuid_count(7u, 0u, &features.exx[4], &features.exx[5], &features.exx[6],
                    &features.exx[7]);
  __get_cpuid(0x80000001u, &features.exx[8], &features.exx[9], &features.exx[10],
              &features.exx[11]);
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
#elif BEDROCK_ARCH_ARM
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
  // 애플 실리콘: sysctlbyname 으로 ARM 기능을 조회해 같은 HWCAP 비트에 채웁니다.
  // (키 이름은 Apple 문서 기준. 실기에서 한 번 검증 권장. SVE/SME 은 미지원=0.)
  auto has = [](const char* name) -> bool {
    int value = 0;
    std::size_t size = sizeof(value);
    return sysctlbyname(name, &value, &size, nullptr, 0) == 0 && value != 0;
  };
  std::uint32_t h = ArmHwcapBits::kFP | ArmHwcapBits::kASIMD;  // 항상 존재
  if (has("hw.optional.arm.FEAT_AES")) h |= ArmHwcapBits::kAES;
  if (has("hw.optional.arm.FEAT_PMULL")) h |= ArmHwcapBits::kPMULL;
  if (has("hw.optional.arm.FEAT_SHA1")) h |= ArmHwcapBits::kSHA1;
  if (has("hw.optional.arm.FEAT_SHA256")) h |= ArmHwcapBits::kSHA2;
  if (has("hw.optional.arm.FEAT_SHA512")) h |= ArmHwcapBits::kSHA512;
  if (has("hw.optional.arm.FEAT_SHA3")) h |= ArmHwcapBits::kSHA3;
  if (has("hw.optional.arm.FEAT_CRC32")) h |= ArmHwcapBits::kCRC32;
  if (has("hw.optional.arm.FEAT_LSE")) h |= ArmHwcapBits::kATOMICS;
  if (has("hw.optional.arm.FEAT_FP16"))
    h |= ArmHwcapBits::kFPHP | ArmHwcapBits::kASIMDHP;
  if (has("hw.optional.arm.FEAT_DotProd")) h |= ArmHwcapBits::kASIMDDP;
  if (has("hw.optional.arm.FEAT_FHM")) h |= ArmHwcapBits::kASIMDFHM;
  if (has("hw.optional.arm.FEAT_RDM")) h |= ArmHwcapBits::kASIMDRDM;
  if (has("hw.optional.arm.FEAT_FCMA")) h |= ArmHwcapBits::kFCMA;
  if (has("hw.optional.arm.FEAT_JSCVT")) h |= ArmHwcapBits::kJSCVT;
  if (has("hw.optional.arm.FEAT_LRCPC")) h |= ArmHwcapBits::kLRCPC;
  if (has("hw.optional.arm.FEAT_DPB")) h |= ArmHwcapBits::kDCPOP;
  if (has("hw.optional.arm.FEAT_DIT")) h |= ArmHwcapBits::kDIT;
  if (has("hw.optional.arm.FEAT_SB")) h |= ArmHwcapBits::kSB;
  if (has("hw.optional.arm.FEAT_SSBS")) h |= ArmHwcapBits::kSSBS;
  if (has("hw.optional.arm.FEAT_FlagM")) h |= ArmHwcapBits::kFLAGM;
  if (has("hw.optional.arm.FEAT_PAuth")) h |= ArmHwcapBits::kPACA;
  features.exx[0] = h;

  std::uint32_t h2 = 0u;
  if (has("hw.optional.arm.FEAT_BF16")) h2 |= ArmHwcap2Bits::k2_BF16;
  if (has("hw.optional.arm.FEAT_I8MM")) h2 |= ArmHwcap2Bits::k2_I8MM;
  if (has("hw.optional.arm.FEAT_FlagM2")) h2 |= ArmHwcap2Bits::k2_FLAGM2;
  if (has("hw.optional.arm.FEAT_FRINTTS")) h2 |= ArmHwcap2Bits::k2_FRINT;
  features.exx[2] = h2;
#endif
#endif
  return features;
}

struct FeatureMaskData {
  std::string_view name;
  Register flag;
};

// This binds names to masks and should be used within this file, and because
// this is just storing hardcoded data, this should be static consexpr
#if BEDROCK_ARCH_X86
static constexpr const std::array<FeatureMaskData, 130> kFeatureAndMask = {
    {{"FPU", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kFPU}},
     {"VME", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kVME}},
     {"DE", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kDE}},
     {"PSE", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kPSE}},
     {"TSC", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kTSC}},
     {"MSR", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kMSR}},
     {"PAE", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kPAE}},
     {"MCE", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kMCE}},
     {"CX8", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kCX8}},
     {"APIC", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kAPIC}},
     {"SEP", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kSEP}},
     {"MTRR", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kMTRR}},
     {"PGE", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kPGE}},
     {"MCA", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kMCA}},
     {"CMOV", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kCMOV}},
     {"PAT", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kPAT}},
     {"PSE36", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kPSE36}},
     {"PSN", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kPSN}},
     {"CLFSH", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kCLFSH}},
     {"DS", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kDS}},
     {"ACPI", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kACPI}},
     {"MMX", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kMMX}},
     {"FXSR", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kFXSR}},
     {"SSE", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kSSE}},
     {"SSE2", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kSSE2}},
     {"SS", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kSS}},
     {"HTT", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kHTT}},
     {"TM", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kTM}},
     {"IA64", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kIA64}},
     {"PBE", {0x0u, 0x0u, 0x0u, FeatureEDXBits::kPBE}},
     {"SSE3", {0x0u, 0x0u, FeatureECXBits::kSSE3, 0x0u}},
     {"PCLMULQDQ", {0x0u, 0x0u, FeatureECXBits::kPCLMULQDQ, 0x0u}},
     {"DTES64", {0x0u, 0x0u, FeatureECXBits::kDTES64, 0x0u}},
     {"MONITOR", {0x0u, 0x0u, FeatureECXBits::kMONITOR, 0x0u}},
     {"DS_CPL", {0x0u, 0x0u, FeatureECXBits::kDS_CPL, 0x0u}},
     {"VMX", {0x0u, 0x0u, FeatureECXBits::kVMX, 0x0u}},
     {"SMX", {0x0u, 0x0u, FeatureECXBits::kSMX, 0x0u}},
     {"EST", {0x0u, 0x0u, FeatureECXBits::kEST, 0x0u}},
     {"TM2", {0x0u, 0x0u, FeatureECXBits::kTM2, 0x0u}},
     {"SSSE3", {0x0u, 0x0u, FeatureECXBits::kSSSE3, 0x0u}},
     {"CNXT_ID", {0x0u, 0x0u, FeatureECXBits::kCNXT_ID, 0x0u}},
     {"SDBG", {0x0u, 0x0u, FeatureECXBits::kSDBG, 0x0u}},
     {"FMA", {0x0u, 0x0u, FeatureECXBits::kFMA, 0x0u}},
     {"CMPXCHG16B", {0x0u, 0x0u, FeatureECXBits::kCMPXCHG16B, 0x0u}},
     {"XTPR", {0x0u, 0x0u, FeatureECXBits::kXTPR, 0x0u}},
     {"PDCM", {0x0u, 0x0u, FeatureECXBits::kPDCM, 0x0u}},
     {"PCID", {0x0u, 0x0u, FeatureECXBits::kPCID, 0x0u}},
     {"DCA", {0x0u, 0x0u, FeatureECXBits::kDCA, 0x0u}},
     {"SSE4_1", {0x0u, 0x0u, FeatureECXBits::kSSE4_1, 0x0u}},
     {"SSE4_2", {0x0u, 0x0u, FeatureECXBits::kSSE4_2, 0x0u}},
     {"X2APIC", {0x0u, 0x0u, FeatureECXBits::kX2APIC, 0x0u}},
     {"MOVBE", {0x0u, 0x0u, FeatureECXBits::kMOVBE, 0x0u}},
     {"POPCNT", {0x0u, 0x0u, FeatureECXBits::kPOPCNT, 0x0u}},
     {"TSC_DEADLINE", {0x0u, 0x0u, FeatureECXBits::kTSC_DEADLINE, 0x0u}},
     {"AESNI", {0x0u, 0x0u, FeatureECXBits::kAESNI, 0x0u}},
     {"XSAVE", {0x0u, 0x0u, FeatureECXBits::kXSAVE, 0x0u}},
     {"OSXSAVE", {0x0u, 0x0u, FeatureECXBits::kOSXSAVE, 0x0u}},
     {"AVX", {0x0u, 0x0u, FeatureECXBits::kAVX, 0x0u}},
     {"F16C", {0x0u, 0x0u, FeatureECXBits::kF16C, 0x0u}},
     {"RDRAND", {0x0u, 0x0u, FeatureECXBits::kRDRAND, 0x0u}},
     {"HYPERVISOR", {0x0u, 0x0u, FeatureECXBits::kHYPERVISOR, 0x0u}},
     {"BMI1", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EBXBits::kBMI1}},
     {"AVX2", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EBXBits::kAVX2}},
     {"BMI2", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EBXBits::kBMI2}},
     {"AVX512F", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EBXBits::kAVX512F}},
     {"AVX512DQ", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EBXBits::kAVX512DQ}},
     {"RDSEED", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EBXBits::kRDSEED}},
     {"ADX", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EBXBits::kADX}},
     {"AVX512_IFMA", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EBXBits::kAVX512_IFMA}},
     {"CLFLUSHOPT", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EBXBits::kCLFLUSHOPT}},
     {"CLWB", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EBXBits::kCLWB}},
     {"AVX512PF", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EBXBits::kAVX512PF}},
     {"AVX512ER", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EBXBits::kAVX512ER}},
     {"AVX512CD", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EBXBits::kAVX512CD}},
     {"SHA", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EBXBits::kSHA}},
     {"AVX512BW", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EBXBits::kAVX512BW}},
     {"AVX512VL", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EBXBits::kAVX512VL}},
     {"PREFETCHWT1", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7ECXBits::kPREFETCHWT1}},
     {"AVX512_VBMI", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7ECXBits::kAVX512_VBMI}},
     {"WAITPKG", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7ECXBits::kWAITPKG}},
     {"AVX512_VBMI2", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7ECXBits::kAVX512_VBMI2}},
     {"GFNI", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7ECXBits::kGFNI}},
     {"VAES", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7ECXBits::kVAES}},
     {"VPCLMULQDQ", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7ECXBits::kVPCLMULQDQ}},
     {"AVX512_VNNI", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7ECXBits::kAVX512_VNNI}},
     {"AVX512_BITALG", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7ECXBits::kAVX512_BITALG}},
     {"AVX512_VPOPCNTDQ", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7ECXBits::kAVX512_VPOPCNTDQ}},
     {"RDPID", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7ECXBits::kRDPID}},
     {"KL", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7ECXBits::kKL}},
     {"CLDEMOTE", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7ECXBits::kCLDEMOTE}},
     {"MOVDIRI", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7ECXBits::kMOVDIRI}},
     {"MOVDIR64B", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7ECXBits::kMOVDIR64B}},
     {"ENQCMD", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7ECXBits::kENQCMD}},
     {"AVX512_4VNNIW", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EDXBits::kAVX512_4VNNIW}},
     {"AVX512_4FMAPS", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EDXBits::kAVX512_4FMAPS}},
     {"FSRM", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EDXBits::kFSRM}},
     {"UINTR", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EDXBits::kUINTR}},
     {"AVX512_VP2INTERSECT", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EDXBits::kAVX512_VP2INTERSECT}},
     {"SERIALIZE", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EDXBits::kSERIALIZE}},
     {"AMX_BF16", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EDXBits::kAMX_BF16}},
     {"AVX512_FP16", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EDXBits::kAVX512_FP16}},
     {"AMX_TILE", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EDXBits::kAMX_TILE}},
     {"AMX_INT8", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7EDXBits::kAMX_INT8}},
     {"LAHF_LM", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureExt1ECXBits::kLAHF_LM}},
     {"LZCNT", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureExt1ECXBits::kLZCNT}},
     {"SSE4A", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureExt1ECXBits::kSSE4A}},
     {"PREFETCHW", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureExt1ECXBits::kPREFETCHW}},
     {"XOP", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureExt1ECXBits::kXOP}},
     {"FMA4", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureExt1ECXBits::kFMA4}},
     {"TBM", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureExt1ECXBits::kTBM}},
     {"SYSCALL", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureExt1EDXBits::kSYSCALL}},
     {"MMXEXT", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureExt1EDXBits::kMMXEXT}},
     {"RDTSCP", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureExt1EDXBits::kRDTSCP}},
     {"LM", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureExt1EDXBits::kLM}},
     {"3DNOWEXT", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureExt1EDXBits::k3DNOWEXT}},
     {"3DNOW", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureExt1EDXBits::k3DNOW}},
     {"SHA512", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7Sub1EAXBits::kSHA512}},
     {"SM3", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7Sub1EAXBits::kSM3}},
     {"SM4", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7Sub1EAXBits::kSM4}},
     {"AVX_VNNI", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7Sub1EAXBits::kAVX_VNNI}},
     {"AVX512_BF16", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7Sub1EAXBits::kAVX512_BF16}},
     {"CMPCCXADD", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7Sub1EAXBits::kCMPCCXADD}},
     {"AMX_FP16", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7Sub1EAXBits::kAMX_FP16}},
     {"AVX_IFMA", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7Sub1EAXBits::kAVX_IFMA}},
     {"MOVRS", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureLeaf7Sub1EAXBits::kMOVRS}},
     {"CLZERO", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureExt8EBXBits::kCLZERO}},
     {"RDPRU", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureExt8EBXBits::kRDPRU}},
     {"WBNOINVD", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureExt8EBXBits::kWBNOINVD}},
     {"PREFETCHI", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureExt21EAXBits::kPREFETCHI}},
     {"AVX512_BMM", {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, FeatureExt21EAXBits::kAVX512_BMM}}}};
#elif BEDROCK_ARCH_ARM
// 이름은 커널 HWCAP 명칭을 따릅니다(예: ASIMD = NEON, PMULL = polynomial multiply).
static constexpr const std::array<FeatureMaskData, 64> kFeatureAndMask = {
    {// ---- HWCAP (exx[0]) ----
     {"FP", {kFP, 0x0u, 0x0u, 0x0u}},
     {"ASIMD", {kASIMD, 0x0u, 0x0u, 0x0u}},
     {"EVTSTRM", {kEVTSTRM, 0x0u, 0x0u, 0x0u}},
     {"AES", {kAES, 0x0u, 0x0u, 0x0u}},
     {"PMULL", {kPMULL, 0x0u, 0x0u, 0x0u}},
     {"SHA1", {kSHA1, 0x0u, 0x0u, 0x0u}},
     {"SHA2", {kSHA2, 0x0u, 0x0u, 0x0u}},
     {"CRC32", {kCRC32, 0x0u, 0x0u, 0x0u}},
     {"ATOMICS", {kATOMICS, 0x0u, 0x0u, 0x0u}},
     {"FPHP", {kFPHP, 0x0u, 0x0u, 0x0u}},
     {"ASIMDHP", {kASIMDHP, 0x0u, 0x0u, 0x0u}},
     {"CPUID", {kCPUID, 0x0u, 0x0u, 0x0u}},
     {"ASIMDRDM", {kASIMDRDM, 0x0u, 0x0u, 0x0u}},
     {"JSCVT", {kJSCVT, 0x0u, 0x0u, 0x0u}},
     {"FCMA", {kFCMA, 0x0u, 0x0u, 0x0u}},
     {"LRCPC", {kLRCPC, 0x0u, 0x0u, 0x0u}},
     {"DCPOP", {kDCPOP, 0x0u, 0x0u, 0x0u}},
     {"SHA3", {kSHA3, 0x0u, 0x0u, 0x0u}},
     {"SM3", {kSM3, 0x0u, 0x0u, 0x0u}},
     {"SM4", {kSM4, 0x0u, 0x0u, 0x0u}},
     {"ASIMDDP", {kASIMDDP, 0x0u, 0x0u, 0x0u}},
     {"SHA512", {kSHA512, 0x0u, 0x0u, 0x0u}},
     {"SVE", {kSVE, 0x0u, 0x0u, 0x0u}},
     {"ASIMDFHM", {kASIMDFHM, 0x0u, 0x0u, 0x0u}},
     {"DIT", {kDIT, 0x0u, 0x0u, 0x0u}},
     {"USCAT", {kUSCAT, 0x0u, 0x0u, 0x0u}},
     {"ILRCPC", {kILRCPC, 0x0u, 0x0u, 0x0u}},
     {"FLAGM", {kFLAGM, 0x0u, 0x0u, 0x0u}},
     {"SSBS", {kSSBS, 0x0u, 0x0u, 0x0u}},
     {"SB", {kSB, 0x0u, 0x0u, 0x0u}},
     {"PACA", {kPACA, 0x0u, 0x0u, 0x0u}},
     {"PACG", {kPACG, 0x0u, 0x0u, 0x0u}},
     // ---- HWCAP2 (exx[2]) ----
     {"DCPODP", {0x0u, 0x0u, k2_DCPODP, 0x0u}},
     {"SVE2", {0x0u, 0x0u, k2_SVE2, 0x0u}},
     {"SVEAES", {0x0u, 0x0u, k2_SVEAES, 0x0u}},
     {"SVEPMULL", {0x0u, 0x0u, k2_SVEPMULL, 0x0u}},
     {"SVEBITPERM", {0x0u, 0x0u, k2_SVEBITPERM, 0x0u}},
     {"SVESHA3", {0x0u, 0x0u, k2_SVESHA3, 0x0u}},
     {"SVESM4", {0x0u, 0x0u, k2_SVESM4, 0x0u}},
     {"FLAGM2", {0x0u, 0x0u, k2_FLAGM2, 0x0u}},
     {"FRINT", {0x0u, 0x0u, k2_FRINT, 0x0u}},
     {"SVEI8MM", {0x0u, 0x0u, k2_SVEI8MM, 0x0u}},
     {"SVEF32MM", {0x0u, 0x0u, k2_SVEF32MM, 0x0u}},
     {"SVEF64MM", {0x0u, 0x0u, k2_SVEF64MM, 0x0u}},
     {"SVEBF16", {0x0u, 0x0u, k2_SVEBF16, 0x0u}},
     {"I8MM", {0x0u, 0x0u, k2_I8MM, 0x0u}},
     {"BF16", {0x0u, 0x0u, k2_BF16, 0x0u}},
     {"DGH", {0x0u, 0x0u, k2_DGH, 0x0u}},
     {"RNG", {0x0u, 0x0u, k2_RNG, 0x0u}},
     {"BTI", {0x0u, 0x0u, k2_BTI, 0x0u}},
     {"MTE", {0x0u, 0x0u, k2_MTE, 0x0u}},
     {"ECV", {0x0u, 0x0u, k2_ECV, 0x0u}},
     {"AFP", {0x0u, 0x0u, k2_AFP, 0x0u}},
     {"RPRES", {0x0u, 0x0u, k2_RPRES, 0x0u}},
     {"MTE3", {0x0u, 0x0u, k2_MTE3, 0x0u}},
     {"SME", {0x0u, 0x0u, k2_SME, 0x0u}},
     {"SME_I16I64", {0x0u, 0x0u, k2_SME_I16I64, 0x0u}},
     {"SME_F64F64", {0x0u, 0x0u, k2_SME_F64F64, 0x0u}},
     {"SME_I8I32", {0x0u, 0x0u, k2_SME_I8I32, 0x0u}},
     {"SME_F16F32", {0x0u, 0x0u, k2_SME_F16F32, 0x0u}},
     {"SME_B16F32", {0x0u, 0x0u, k2_SME_B16F32, 0x0u}},
     {"SME_F32F32", {0x0u, 0x0u, k2_SME_F32F32, 0x0u}},
     {"SME_FA64", {0x0u, 0x0u, k2_SME_FA64, 0x0u}},
     {"WFXT", {0x0u, 0x0u, k2_WFXT, 0x0u}}}};
#else
static constexpr const std::array<FeatureMaskData, 0> kFeatureAndMask = {};
#endif

bool IsCpuEnabledFeature(Register cpu_feature_flag,
                         std::string_view feature_name) {
  bool enabled = false;

  for (const FeatureMaskData& it : kFeatureAndMask) {
    if (it.name != feature_name) {
      continue;
    }

    enabled = true;
    for (std::uint32_t i = 0; i < it.flag.exx.size(); i++) {
      if (it.flag.exx[i] != 0x0u) {
        enabled =
            enabled && ((cpu_feature_flag.exx[i] & it.flag.exx[i]) != 0x0u);
      }
    }

    break;
  }

  return enabled;
}

};  // namespace bedrock::intrinsic
