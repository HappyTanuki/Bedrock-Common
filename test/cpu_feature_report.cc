/**
 * @file cpu_feature_report.cc
 * @brief 실행 CPU/OS가 지원하는 CpuFeature를 모두 출력하는 정보성
 *        테스트.
 *
 * kAllFeatures 표를 순회하며 각 기능의 지원 여부를 [O]/[ ] 로 표시해
 * 출력하고, 지원 개수를 총 개수와 함께 보여준다. AVX 계열은
 * HasFeature 내부에서 XCR0(OS 상태) 검증까지 거친 결과다.
 *
 * @return 하드웨어 지원 여부와 무관하게 항상 0(정보 출력이 목적).
 */
#include <cstddef>
#include <iostream>
#include <string_view>
#include <utility>

#include "common/intrinsics.h"

namespace {

using bedrock::intrinsic::CpuFeature;
using bedrock::intrinsic::HasFeature;

/**
 * @brief CpuFeature 전체 목록 — enum 값과 이름 문자열의 매핑 표.
 *
 * C++에는 enum 리플렉션이 없어 이름을 함께 나열한다. 선언 순서는
 * common/intrinsics.h 의 enum 선언 순서와 동일하다.
 */
// clang-format off
constexpr std::pair<CpuFeature, std::string_view> kAllFeatures[] = {
    {CpuFeature::kFPU,                  "FPU"},
    {CpuFeature::kVME,                  "VME"},
    {CpuFeature::kDE,                   "DE"},
    {CpuFeature::kPSE,                  "PSE"},
    {CpuFeature::kTSC,                  "TSC"},
    {CpuFeature::kMSR,                  "MSR"},
    {CpuFeature::kPAE,                  "PAE"},
    {CpuFeature::kMCE,                  "MCE"},
    {CpuFeature::kCX8,                  "CX8"},
    {CpuFeature::kAPIC,                 "APIC"},
    {CpuFeature::kSEP,                  "SEP"},
    {CpuFeature::kMTRR,                 "MTRR"},
    {CpuFeature::kPGE,                  "PGE"},
    {CpuFeature::kMCA,                  "MCA"},
    {CpuFeature::kCMOV,                 "CMOV"},
    {CpuFeature::kPAT,                  "PAT"},
    {CpuFeature::kPSE36,                "PSE36"},
    {CpuFeature::kPSN,                  "PSN"},
    {CpuFeature::kCLFSH,                "CLFSH"},
    {CpuFeature::kDS,                   "DS"},
    {CpuFeature::kACPI,                 "ACPI"},
    {CpuFeature::kMMX,                  "MMX"},
    {CpuFeature::kFXSR,                 "FXSR"},
    {CpuFeature::kSSE,                  "SSE"},
    {CpuFeature::kSSE2,                 "SSE2"},
    {CpuFeature::kSS,                   "SS"},
    {CpuFeature::kHTT,                  "HTT"},
    {CpuFeature::kTM,                   "TM"},
    {CpuFeature::kIA64,                 "IA64"},
    {CpuFeature::kPBE,                  "PBE"},
    {CpuFeature::kSSE3,                 "SSE3"},
    {CpuFeature::kPCLMULQDQ,            "PCLMULQDQ"},
    {CpuFeature::kDTES64,               "DTES64"},
    {CpuFeature::kMONITOR,              "MONITOR"},
    {CpuFeature::kDS_CPL,               "DS_CPL"},
    {CpuFeature::kVMX,                  "VMX"},
    {CpuFeature::kSMX,                  "SMX"},
    {CpuFeature::kEST,                  "EST"},
    {CpuFeature::kTM2,                  "TM2"},
    {CpuFeature::kSSSE3,                "SSSE3"},
    {CpuFeature::kCNXT_ID,              "CNXT_ID"},
    {CpuFeature::kSDBG,                 "SDBG"},
    {CpuFeature::kFMA,                  "FMA"},
    {CpuFeature::kCMPXCHG16B,           "CMPXCHG16B"},
    {CpuFeature::kXTPR,                 "XTPR"},
    {CpuFeature::kPDCM,                 "PDCM"},
    {CpuFeature::kPCID,                 "PCID"},
    {CpuFeature::kDCA,                  "DCA"},
    {CpuFeature::kSSE4_1,               "SSE4_1"},
    {CpuFeature::kSSE4_2,               "SSE4_2"},
    {CpuFeature::kX2APIC,               "X2APIC"},
    {CpuFeature::kMOVBE,                "MOVBE"},
    {CpuFeature::kPOPCNT,               "POPCNT"},
    {CpuFeature::kTSC_DEADLINE,         "TSC_DEADLINE"},
    {CpuFeature::kAESNI,                "AESNI"},
    {CpuFeature::kXSAVE,                "XSAVE"},
    {CpuFeature::kOSXSAVE,              "OSXSAVE"},
    {CpuFeature::kAVX,                  "AVX"},
    {CpuFeature::kF16C,                 "F16C"},
    {CpuFeature::kRDRAND,               "RDRAND"},
    {CpuFeature::kHYPERVISOR,           "HYPERVISOR"},
    {CpuFeature::kBMI1,                 "BMI1"},
    {CpuFeature::kAVX2,                 "AVX2"},
    {CpuFeature::kBMI2,                 "BMI2"},
    {CpuFeature::kAVX512F,              "AVX512F"},
    {CpuFeature::kAVX512DQ,             "AVX512DQ"},
    {CpuFeature::kRDSEED,               "RDSEED"},
    {CpuFeature::kADX,                  "ADX"},
    {CpuFeature::kAVX512_IFMA,          "AVX512_IFMA"},
    {CpuFeature::kCLFLUSHOPT,           "CLFLUSHOPT"},
    {CpuFeature::kCLWB,                 "CLWB"},
    {CpuFeature::kAVX512PF,             "AVX512PF"},
    {CpuFeature::kAVX512ER,             "AVX512ER"},
    {CpuFeature::kAVX512CD,             "AVX512CD"},
    {CpuFeature::kSHA,                  "SHA"},
    {CpuFeature::kAVX512BW,             "AVX512BW"},
    {CpuFeature::kAVX512VL,             "AVX512VL"},
    {CpuFeature::kPREFETCHWT1,          "PREFETCHWT1"},
    {CpuFeature::kAVX512_VBMI,          "AVX512_VBMI"},
    {CpuFeature::kWAITPKG,              "WAITPKG"},
    {CpuFeature::kAVX512_VBMI2,         "AVX512_VBMI2"},
    {CpuFeature::kGFNI,                 "GFNI"},
    {CpuFeature::kVAES,                 "VAES"},
    {CpuFeature::kVPCLMULQDQ,           "VPCLMULQDQ"},
    {CpuFeature::kAVX512_VNNI,          "AVX512_VNNI"},
    {CpuFeature::kAVX512_BITALG,        "AVX512_BITALG"},
    {CpuFeature::kAVX512_VPOPCNTDQ,     "AVX512_VPOPCNTDQ"},
    {CpuFeature::kRDPID,                "RDPID"},
    {CpuFeature::kKL,                   "KL"},
    {CpuFeature::kCLDEMOTE,             "CLDEMOTE"},
    {CpuFeature::kMOVDIRI,              "MOVDIRI"},
    {CpuFeature::kMOVDIR64B,            "MOVDIR64B"},
    {CpuFeature::kENQCMD,               "ENQCMD"},
    {CpuFeature::kAVX512_4VNNIW,        "AVX512_4VNNIW"},
    {CpuFeature::kAVX512_4FMAPS,        "AVX512_4FMAPS"},
    {CpuFeature::kFSRM,                 "FSRM"},
    {CpuFeature::kUINTR,                "UINTR"},
    {CpuFeature::kAVX512_VP2INTERSECT,  "AVX512_VP2INTERSECT"},
    {CpuFeature::kSERIALIZE,            "SERIALIZE"},
    {CpuFeature::kAMX_BF16,             "AMX_BF16"},
    {CpuFeature::kAVX512_FP16,          "AVX512_FP16"},
    {CpuFeature::kAMX_TILE,             "AMX_TILE"},
    {CpuFeature::kAMX_INT8,             "AMX_INT8"},
    {CpuFeature::kLAHF_LM,              "LAHF_LM"},
    {CpuFeature::kLZCNT,                "LZCNT"},
    {CpuFeature::kSSE4A,                "SSE4A"},
    {CpuFeature::kPREFETCHW,            "PREFETCHW"},
    {CpuFeature::kXOP,                  "XOP"},
    {CpuFeature::kFMA4,                 "FMA4"},
    {CpuFeature::kTBM,                  "TBM"},
    {CpuFeature::kSYSCALL,              "SYSCALL"},
    {CpuFeature::kMMXEXT,               "MMXEXT"},
    {CpuFeature::kRDTSCP,               "RDTSCP"},
    {CpuFeature::kLM,                   "LM"},
    {CpuFeature::k3DNOWEXT,             "3DNOWEXT"},
    {CpuFeature::k3DNOW,                "3DNOW"},
    {CpuFeature::kSHA512,               "SHA512"},
    {CpuFeature::kSM3,                  "SM3"},
    {CpuFeature::kSM4,                  "SM4"},
    {CpuFeature::kAVX_VNNI,             "AVX_VNNI"},
    {CpuFeature::kAVX512_BF16,          "AVX512_BF16"},
    {CpuFeature::kCMPCCXADD,            "CMPCCXADD"},
    {CpuFeature::kAMX_FP16,             "AMX_FP16"},
    {CpuFeature::kAVX_IFMA,             "AVX_IFMA"},
    {CpuFeature::kMOVRS,                "MOVRS"},
    {CpuFeature::kCLZERO,               "CLZERO"},
    {CpuFeature::kRDPRU,                "RDPRU"},
    {CpuFeature::kWBNOINVD,             "WBNOINVD"},
    {CpuFeature::kPREFETCHI,            "PREFETCHI"},
    {CpuFeature::kAVX512_BMM,           "AVX512_BMM"},
    {CpuFeature::kFP,                   "FP"},
    {CpuFeature::kASIMD,                "ASIMD"},
    {CpuFeature::kEVTSTRM,              "EVTSTRM"},
    {CpuFeature::kAES,                  "AES"},
    {CpuFeature::kPMULL,                "PMULL"},
    {CpuFeature::kSHA1,                 "SHA1"},
    {CpuFeature::kSHA2,                 "SHA2"},
    {CpuFeature::kCRC32,                "CRC32"},
    {CpuFeature::kATOMICS,              "ATOMICS"},
    {CpuFeature::kFPHP,                 "FPHP"},
    {CpuFeature::kASIMDHP,              "ASIMDHP"},
    {CpuFeature::kCPUID,                "CPUID"},
    {CpuFeature::kASIMDRDM,             "ASIMDRDM"},
    {CpuFeature::kJSCVT,                "JSCVT"},
    {CpuFeature::kFCMA,                 "FCMA"},
    {CpuFeature::kLRCPC,                "LRCPC"},
    {CpuFeature::kDCPOP,                "DCPOP"},
    {CpuFeature::kSHA3,                 "SHA3"},
    {CpuFeature::kASIMDDP,              "ASIMDDP"},
    {CpuFeature::kSVE,                  "SVE"},
    {CpuFeature::kASIMDFHM,             "ASIMDFHM"},
    {CpuFeature::kDIT,                  "DIT"},
    {CpuFeature::kUSCAT,                "USCAT"},
    {CpuFeature::kILRCPC,               "ILRCPC"},
    {CpuFeature::kFLAGM,                "FLAGM"},
    {CpuFeature::kSSBS,                 "SSBS"},
    {CpuFeature::kSB,                   "SB"},
    {CpuFeature::kPACA,                 "PACA"},
    {CpuFeature::kPACG,                 "PACG"},
    {CpuFeature::kDCPODP,               "DCPODP"},
    {CpuFeature::kSVE2,                 "SVE2"},
    {CpuFeature::kSVEAES,               "SVEAES"},
    {CpuFeature::kSVEPMULL,             "SVEPMULL"},
    {CpuFeature::kSVEBITPERM,           "SVEBITPERM"},
    {CpuFeature::kSVESHA3,              "SVESHA3"},
    {CpuFeature::kSVESM4,               "SVESM4"},
    {CpuFeature::kFLAGM2,               "FLAGM2"},
    {CpuFeature::kFRINT,                "FRINT"},
    {CpuFeature::kSVEI8MM,              "SVEI8MM"},
    {CpuFeature::kSVEF32MM,             "SVEF32MM"},
    {CpuFeature::kSVEF64MM,             "SVEF64MM"},
    {CpuFeature::kSVEBF16,              "SVEBF16"},
    {CpuFeature::kI8MM,                 "I8MM"},
    {CpuFeature::kBF16,                 "BF16"},
    {CpuFeature::kDGH,                  "DGH"},
    {CpuFeature::kRNG,                  "RNG"},
    {CpuFeature::kBTI,                  "BTI"},
    {CpuFeature::kMTE,                  "MTE"},
    {CpuFeature::kECV,                  "ECV"},
    {CpuFeature::kAFP,                  "AFP"},
    {CpuFeature::kRPRES,                "RPRES"},
    {CpuFeature::kMTE3,                 "MTE3"},
    {CpuFeature::kSME,                  "SME"},
    {CpuFeature::kSME_I16I64,           "SME_I16I64"},
    {CpuFeature::kSME_F64F64,           "SME_F64F64"},
    {CpuFeature::kSME_I8I32,            "SME_I8I32"},
    {CpuFeature::kSME_F16F32,           "SME_F16F32"},
    {CpuFeature::kSME_B16F32,           "SME_B16F32"},
    {CpuFeature::kSME_F32F32,           "SME_F32F32"},
    {CpuFeature::kSME_FA64,             "SME_FA64"},
    {CpuFeature::kWFXT,                 "WFXT"},
};
// clang-format on

}  // namespace

/** @brief 지원되는 CPU 기능을 모두 나열해 콘솔에 출력한다. */
int main() {
  const auto reg = bedrock::intrinsic::GetCPUFeatures();

  std::size_t supported = 0;
  for (const auto& [feature, name] : kAllFeatures) {
    const bool enabled = HasFeature(reg, feature);
    if (enabled) {
      ++supported;
    }
    std::cout << (enabled ? "[O] " : "[ ] ") << name << "\n";
  }
  std::cout << supported << " / " << std::size(kAllFeatures)
            << " features supported" << std::endl;

  return 0;
}
