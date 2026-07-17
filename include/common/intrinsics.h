#pragma once
#include <array>
#include <cstdint>

// ============================================================
// Architecture detection
// ============================================================
// 아키텍처 인식은 전적으로 CMake가 담당합니다(.cmake/architecture.cmake).
// CMake가 bedrock_common_config.h 를 생성하여 BEDROCK_ARCH_X86 /
// BEDROCK_ARCH_ARM / BEDROCK_ARCH_NAME 매크로를 주입하고, 이 헤더는 그 값을
// 그대로 신뢰합니다. (소스 차원의 컴파일러 사전정의 매크로 감지는 사용하지
// 않습니다.) bedrock_common_config.h 는 빌드 디렉터리에 생성되어 SYSTEM PUBLIC
// 인클루드 경로로 노출되므로, 이 라이브러리를 사용하는 측에도 전이적으로
// 제공됩니다.
#include <bedrock_common_config.h>

#if BEDROCK_ARCH_X86
#ifndef _WIN32
#include <cpuid.h>
#else
#include <intrin.h>
#endif
// x86 전용
#include <immintrin.h>
#endif  // BEDROCK_ARCH_X86

// ARM의 런타임 기능 인식은 CPUID가 아니라 OS가 제공하는 정보로 합니다.
//   Linux   : getauxval(AT_HWCAP/AT_HWCAP2)
//   macOS   : sysctlbyname("hw.optional.arm.FEAT_*")  (애플 실리콘)
//   Windows : IsProcessorFeaturePresent(PF_ARM_*)  (Windows on ARM)
//   그 외   : 미구현 → 0
// (<windows.h>는 매크로 오염(min/max 등)이 심해 헤더에서 include하지 않고
//  intrinsics.cc 내부에서만 include합니다.)
#if BEDROCK_ARCH_ARM
#if defined(__linux__)
#include <sys/auxv.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#endif

namespace bedrock::intrinsic {

struct Register {
  // 저장 레이아웃:
  //   x86: [0..3]=CPUID leaf1(EAX/EBX/ECX/EDX),
  //        [4..7]=CPUID leaf7 서브리프0, [8..11]=CPUID 확장 leaf 0x80000001
  //   ARM: [0]=HWCAP lo,[1]=HWCAP hi,[2]=HWCAP2 lo,[3]=HWCAP2 hi
  //        [12..15]=leaf7 서브리프1, [16..19]=0x80000008, [20..23]=0x80000021
  std::array<std::uint32_t, 24> exx = {};

  constexpr std::uint32_t& eax() { return exx[0]; }
  constexpr std::uint32_t& ebx() { return exx[1]; }
  constexpr std::uint32_t& ecx() { return exx[2]; }
  constexpr std::uint32_t& edx() { return exx[3]; }
};


// CPU 기능(명령어셋 확장 + 기능 플래그) 식별자.
// 이름은 Intel SDM / AMD APM / Linux hwcap.h 의 명칭을 그대로 따릅니다.
// x86과 ARM 이름을 모두 포함하며(중복 이름은 공유), 현재 아키텍처의
// 테이블에 없는 항목을 조회하면 HasFeature는 false를 반환합니다.
enum class CpuFeature {
  kFPU, kVME, kDE, kPSE,
  kTSC, kMSR, kPAE, kMCE,
  kCX8, kAPIC, kSEP, kMTRR,
  kPGE, kMCA, kCMOV, kPAT,
  kPSE36, kPSN, kCLFSH, kDS,
  kACPI, kMMX, kFXSR, kSSE,
  kSSE2, kSS, kHTT, kTM,
  kIA64, kPBE, kSSE3, kPCLMULQDQ,
  kDTES64, kMONITOR, kDS_CPL, kVMX,
  kSMX, kEST, kTM2, kSSSE3,
  kCNXT_ID, kSDBG, kFMA, kCMPXCHG16B,
  kXTPR, kPDCM, kPCID, kDCA,
  kSSE4_1, kSSE4_2, kX2APIC, kMOVBE,
  kPOPCNT, kTSC_DEADLINE, kAESNI, kXSAVE,
  kOSXSAVE, kAVX, kF16C, kRDRAND,
  kHYPERVISOR, kBMI1, kAVX2, kBMI2,
  kAVX512F, kAVX512DQ, kRDSEED, kADX,
  kAVX512_IFMA, kCLFLUSHOPT, kCLWB, kAVX512PF,
  kAVX512ER, kAVX512CD, kSHA, kAVX512BW,
  kAVX512VL, kPREFETCHWT1, kAVX512_VBMI, kWAITPKG,
  kAVX512_VBMI2, kGFNI, kVAES, kVPCLMULQDQ,
  kAVX512_VNNI, kAVX512_BITALG, kAVX512_VPOPCNTDQ, kRDPID,
  kKL, kCLDEMOTE, kMOVDIRI, kMOVDIR64B,
  kENQCMD, kAVX512_4VNNIW, kAVX512_4FMAPS, kFSRM,
  kUINTR, kAVX512_VP2INTERSECT, kSERIALIZE, kAMX_BF16,
  kAVX512_FP16, kAMX_TILE, kAMX_INT8, kLAHF_LM,
  kLZCNT, kSSE4A, kPREFETCHW, kXOP,
  kFMA4, kTBM, kSYSCALL, kMMXEXT,
  kRDTSCP, kLM, k3DNOWEXT, k3DNOW,
  kSHA512, kSM3, kSM4, kAVX_VNNI,
  kAVX512_BF16, kCMPCCXADD, kAMX_FP16, kAVX_IFMA,
  kMOVRS, kCLZERO, kRDPRU, kWBNOINVD,
  kPREFETCHI, kAVX512_BMM, kFP, kASIMD,
  kEVTSTRM, kAES, kPMULL, kSHA1,
  kSHA2, kCRC32, kATOMICS, kFPHP,
  kASIMDHP, kCPUID, kASIMDRDM, kJSCVT,
  kFCMA, kLRCPC, kDCPOP, kSHA3,
  kASIMDDP, kSVE, kASIMDFHM, kDIT,
  kUSCAT, kILRCPC, kFLAGM, kSSBS,
  kSB, kPACA, kPACG, kDCPODP,
  kSVE2, kSVEAES, kSVEPMULL, kSVEBITPERM,
  kSVESHA3, kSVESM4, kFLAGM2, kFRINT,
  kSVEI8MM, kSVEF32MM, kSVEF64MM, kSVEBF16,
  kI8MM, kBF16, kDGH, kRNG,
  kBTI, kMTE, kECV, kAFP,
  kRPRES, kMTE3, kSME, kSME_I16I64,
  kSME_F64F64, kSME_I8I32, kSME_F16F32, kSME_B16F32,
  kSME_F32F32, kSME_FA64, kWFXT,
};

Register GetCPUFeatures();

bool HasFeature(const Register& cpu_feature_flag, CpuFeature feature);

};  // namespace bedrock::intrinsic
