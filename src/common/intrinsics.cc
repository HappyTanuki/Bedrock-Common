#include "common/intrinsics.h"

#include <array>

#ifndef _WIN32
#include <cpuid.h>
#else
#include <intrin.h>
#endif

namespace bedrock::intrinsic {

// CPUID 명령어를 사용하여 CPU가 지원하는 명령어셋을 감지합니다.
Register GetCPUFeatures() {
  Register features = {};
#ifndef _WIN32
  __get_cpuid(1, &features.eax(), &features.ebx(), &features.ecx(),
              &features.edx());
#else
  __cpuid(static_cast<int*>(features.exx), 1);
#endif
  return features;
}

struct FeatureMaskData {
  std::string_view name;
  Register flag;
};

// This binds names to masks and should be used within this file, and because
// this is just storing hardcoded data, this should be static consexpr
static constexpr const std::array<FeatureMaskData, 61> kFeatureAndMask = {
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
     {"HYPERVISOR", {0x0u, 0x0u, FeatureECXBits::kHYPERVISOR, 0x0u}}}};

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
