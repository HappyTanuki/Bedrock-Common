#ifndef BEDROCK_COMMON_COMMON_INTRINSICS_H_
#define BEDROCK_COMMON_COMMON_INTRINSICS_H_
#include <array>
#include <cstdint>
#include <string_view>

// ============================================================
// Architecture detection
// ============================================================
// 아키텍처 인식은 전적으로 CMake가 담당합니다(.cmake/architecture.cmake).
// CMake가 config.h 를 생성하여 BEDROCK_ARCH_X86 / BEDROCK_ARCH_ARM /
// BEDROCK_ARCH_NAME 매크로를 주입하고, 이 헤더는 그 값을 그대로 신뢰합니다.
// (소스 차원의 컴파일러 사전정의 매크로 감지는 사용하지 않습니다.)
// config.h 는 빌드 디렉터리에 생성되어 SYSTEM PUBLIC 인클루드 경로로 노출되므로,
// 이 라이브러리를 사용하는 측에도 전이적으로 제공됩니다.
#include <config.h>

// CPUID 및 x86 인트린식 헤더는 x86/x64 전용입니다. ARM 등에서는 존재하지
// 않으므로, 감지된 아키텍처로 해당 헤더를 가드합니다(미가드 시 ARM 빌드 실패).
#if BEDROCK_ARCH_X86
#ifndef _WIN32
#include <cpuid.h>
#else
#include <intrin.h>
#endif
// 인트린식 선언 헤더. 포함만으로는 명령어가 생성되지 않으므로(선언일 뿐)
// 안전하지만, x86 전용이라 아키텍처 가드 안에 둡니다.
#include <immintrin.h>
#endif  // BEDROCK_ARCH_X86

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

#if BEDROCK_ARCH_X86
enum FeatureECXBits {
  kSSE3 = 1u << 0,
  kPCLMULQDQ = 1u << 1,
  kDTES64 = 1u << 2,
  kMONITOR = 1u << 3,
  kDS_CPL = 1u << 4,
  kVMX = 1u << 5,
  kSMX = 1u << 6,
  kEST = 1u << 7,
  kTM2 = 1u << 8,
  kSSSE3 = 1u << 9,
  kCNXT_ID = 1u << 10,
  kSDBG = 1u << 11,
  kFMA = 1u << 12,
  kCMPXCHG16B = 1u << 13,
  kXTPR = 1u << 14,
  kPDCM = 1u << 15,
  kPCID = 1u << 17,
  kDCA = 1u << 18,
  kSSE4_1 = 1u << 19,
  kSSE4_2 = 1u << 20,
  kX2APIC = 1u << 21,
  kMOVBE = 1u << 22,
  kPOPCNT = 1u << 23,
  kTSC_DEADLINE = 1u << 24,
  kAESNI = 1u << 25,
  kXSAVE = 1u << 26,
  kOSXSAVE = 1u << 27,
  kAVX = 1u << 28,
  kF16C = 1u << 29,
  kRDRAND = 1u << 30,
  kHYPERVISOR = 1u << 31
};

enum FeatureEDXBits {
  kFPU = 1u << 0,
  kVME = 1u << 1,
  kDE = 1u << 2,
  kPSE = 1u << 3,
  kTSC = 1u << 4,
  kMSR = 1u << 5,
  kPAE = 1u << 6,
  kMCE = 1u << 7,
  kCX8 = 1u << 8,
  kAPIC = 1u << 9,
  kSEP = 1u << 11,
  kMTRR = 1u << 12,
  kPGE = 1u << 13,
  kMCA = 1u << 14,
  kCMOV = 1u << 15,
  kPAT = 1u << 16,
  kPSE36 = 1u << 17,
  kPSN = 1u << 18,
  kCLFSH = 1u << 19,
  kDS = 1u << 21,
  kACPI = 1u << 22,
  kMMX = 1u << 23,
  kFXSR = 1u << 24,
  kSSE = 1u << 25,
  kSSE2 = 1u << 26,
  kSS = 1u << 27,
  kHTT = 1u << 28,
  kTM = 1u << 29,
  kIA64 = 1u << 30,
  kPBE = 1u << 31
};

// CPUID leaf 7, subleaf 0, EBX (Intel SDM Vol.2A). 저장: exx[5]
enum FeatureLeaf7EBXBits {
  kBMI1 = 1u << 3,
  kAVX2 = 1u << 5,
  kBMI2 = 1u << 8,
  kAVX512F = 1u << 16,
  kAVX512DQ = 1u << 17,
  kRDSEED = 1u << 18,
  kADX = 1u << 19,
  kAVX512_IFMA = 1u << 21,
  kCLFLUSHOPT = 1u << 23,
  kCLWB = 1u << 24,
  kAVX512PF = 1u << 26,
  kAVX512ER = 1u << 27,
  kAVX512CD = 1u << 28,
  kSHA = 1u << 29,
  kAVX512BW = 1u << 30,
  kAVX512VL = 1u << 31
};

// CPUID leaf 7, subleaf 0, ECX. 저장: exx[6]
enum FeatureLeaf7ECXBits {
  kPREFETCHWT1 = 1u << 0,
  kAVX512_VBMI = 1u << 1,
  kWAITPKG = 1u << 5,
  kAVX512_VBMI2 = 1u << 6,
  kGFNI = 1u << 8,
  kVAES = 1u << 9,
  kVPCLMULQDQ = 1u << 10,
  kAVX512_VNNI = 1u << 11,
  kAVX512_BITALG = 1u << 12,
  kAVX512_VPOPCNTDQ = 1u << 14,
  kRDPID = 1u << 22,
  kKL = 1u << 23,
  kCLDEMOTE = 1u << 25,
  kMOVDIRI = 1u << 27,
  kMOVDIR64B = 1u << 28,
  kENQCMD = 1u << 29
};

// CPUID leaf 7, subleaf 0, EDX. 저장: exx[7]
enum FeatureLeaf7EDXBits {
  kAVX512_4VNNIW = 1u << 2,
  kAVX512_4FMAPS = 1u << 3,
  kFSRM = 1u << 4,
  kUINTR = 1u << 5,
  kAVX512_VP2INTERSECT = 1u << 8,
  kSERIALIZE = 1u << 14,
  kAMX_BF16 = 1u << 22,
  kAVX512_FP16 = 1u << 23,
  kAMX_TILE = 1u << 24,
  kAMX_INT8 = 1u << 25
};

// CPUID 확장 leaf 0x80000001, ECX (AMD APM Vol.3 App.E). 저장: exx[10]
enum FeatureExt1ECXBits {
  kLAHF_LM = 1u << 0,
  kLZCNT = 1u << 5,
  kSSE4A = 1u << 6,
  kPREFETCHW = 1u << 8,
  kXOP = 1u << 11,
  kFMA4 = 1u << 16,
  kTBM = 1u << 21
};

// CPUID 확장 leaf 0x80000001, EDX. 저장: exx[11]
enum FeatureExt1EDXBits {
  kSYSCALL = 1u << 11,
  kMMXEXT = 1u << 22,
  kRDTSCP = 1u << 27,
  kLM = 1u << 29,
  k3DNOWEXT = 1u << 30,
  k3DNOW = 1u << 31
};

// CPUID leaf 7, subleaf 1, EAX (Intel SDM Vol.2A). 저장: exx[12]
enum FeatureLeaf7Sub1EAXBits {
  kSHA512 = 1u << 0,
  kSM3 = 1u << 1,
  kSM4 = 1u << 2,
  kAVX_VNNI = 1u << 4,
  kAVX512_BF16 = 1u << 5,
  kCMPCCXADD = 1u << 7,
  kAMX_FP16 = 1u << 21,
  kAVX_IFMA = 1u << 23,
  kMOVRS = 1u << 31
};

// CPUID 확장 leaf 0x80000008, EBX (AMD APM Vol.3 App.E). 저장: exx[17]
enum FeatureExt8EBXBits {
  kCLZERO = 1u << 0,
  kRDPRU = 1u << 4,
  kWBNOINVD = 1u << 9
};

// CPUID 확장 leaf 0x80000021, EAX. 저장: exx[20]
enum FeatureExt21EAXBits {
  kPREFETCHI = 1u << 20,
  kAVX512_BMM = 1u << 23
};
#endif  // BEDROCK_ARCH_X86

Register GetCPUFeatures();

bool IsCpuEnabledFeature(Register cpu_feature_flag,
                         std::string_view feature_name);

};  // namespace bedrock::intrinsic

#endif
