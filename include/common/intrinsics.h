#ifndef BEDROCK_COMMON_COMMON_INTRINSICS_H_
#define BEDROCK_COMMON_COMMON_INTRINSICS_H_
#include <array>
#include <cstdint>
#include <string_view>

namespace bedrock::intrinsic {

struct Register {
  std::array<std::uint32_t, 4> exx = {};

  constexpr std::uint32_t& eax() { return exx[0]; }
  constexpr std::uint32_t& ebx() { return exx[1]; }
  constexpr std::uint32_t& ecx() { return exx[2]; }
  constexpr std::uint32_t& edx() { return exx[3]; }
};

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

Register GetCPUFeatures();

bool IsCpuEnabledFeature(Register cpu_feature_flag,
                         std::string_view feature_name);

};  // namespace bedrock::intrinsic

#endif
