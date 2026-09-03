/**
 * @file intrinsics.h
 * @brief CPU 아키텍처·기능(명령어셋) 런타임 감지 API.
 *
 * x86(CPUID)과 ARM(HWCAP 등) 아키텍처의 CPU 기능 지원 여부를 동일한
 * 인터페이스(Register, CpuFeature, GetCPUFeatures(), HasFeature())로
 * 조회할 수 있게 합니다. 아키텍처별 감지 구현은 intrinsics.cc에 둡니다.
 */
#pragma once
#include <array>
#include <cstdint>

/**
 * @brief ARM 런타임 CPU 기능 인식 방식.
 *
 * ARM의 런타임 기능 인식은 CPUID가 아니라 OS가 제공하는 정보로
 * 합니다.
 *   - Linux   : getauxval(AT_HWCAP/AT_HWCAP2)
 *   - macOS   : sysctlbyname("hw.optional.arm.FEAT_*")  (애플 실리콘)
 *   - Windows : IsProcessorFeaturePresent(PF_ARM_*)  (Windows on ARM)
 *   - 그 외   : 미구현 -> 0
 *
 * @note 플랫폼 header는 모두 intrinsics.cc 내부에서만 include합니다.
 */
namespace bedrock::intrinsic {

/**
 * @brief CPU 기능 감지 결과를 담는 아키텍처별 원시 레지스터 저장소.
 *
 * GetCPUFeatures() 가 채우고 HasFeature() 가 해석하는 불투명한
 * 값입니다. 값 자체의 비트 의미는 x86/ARM 여부에 따라 다르므로
 * (exx 문서 참고) 반드시 HasFeature() 를 통해서만 조회해야 합니다.
 */
struct Register {
  /**
   * @brief 아키텍처별 원시 레지스터 저장 슬롯.
   *
   * 저장 레이아웃:
   *   x86: [0..3]=CPUID leaf1(EAX/EBX/ECX/EDX),
   *        [4..7]=CPUID leaf7 서브리프0, [8..11]=CPUID 확장 leaf
   *        0x80000001
   *   ARM: [0]=HWCAP lo,[1]=HWCAP hi,[2]=HWCAP2 lo,[3]=HWCAP2 hi
   *        [12..15]=leaf7 서브리프1, [16..19]=0x80000008,
   *        [20..23]=0x80000021
   */
  std::array<std::uint32_t, 24> exx = {};

  /** @brief exx[0] 참조(x86 CPUID leaf1 EAX). */
  constexpr std::uint32_t& Eax() { return exx[0]; }
  /** @brief exx[1] 참조(x86 CPUID leaf1 EBX). */
  constexpr std::uint32_t& Ebx() { return exx[1]; }
  /** @brief exx[2] 참조(x86 CPUID leaf1 ECX). */
  constexpr std::uint32_t& Ecx() { return exx[2]; }
  /** @brief exx[3] 참조(x86 CPUID leaf1 EDX). */
  constexpr std::uint32_t& Edx() { return exx[3]; }
};

/**
 * @brief CPU 기능(명령어셋 확장 + 기능 플래그) 식별자.
 *
 * 이름은 Intel SDM / AMD APM / Linux hwcap.h 의 명칭을 그대로
 * 따릅니다. x86과 ARM 이름을 모두 포함하며(중복 이름은 공유), 현재
 * 아키텍처의 테이블에 없는 항목을 조회하면 HasFeature는 false를
 * 반환합니다.
 *
 * @note 개별 열거자는 각 표준(Intel SDM / AMD APM / Linux hwcap.h)의
 *       명칭을 그대로 사용하는 식별자이며, 정확한 {레지스터, 비트}
 *       매핑은 src/common/intrinsics.cc 의 kFeatureAndMask(및 XCR0
 *       요구사항은 kRequiredXcr0)에 단일 진실 공급원으로 정의되어
 *       있습니다. 열거자 개수가 많아(약 190개) 이 헤더에서는 개별
 *       열거자별 설명을 생략합니다.
 */
enum class CpuFeature : std::uint8_t {
  kFPU,
  kVME,
  kDE,
  kPSE,
  kTSC,
  kMSR,
  kPAE,
  kMCE,
  kCX8,
  kAPIC,
  kSEP,
  kMTRR,
  kPGE,
  kMCA,
  kCMOV,
  kPAT,
  kPSE36,
  kPSN,
  kCLFSH,
  kDS,
  kACPI,
  kMMX,
  kFXSR,
  kSSE,
  kSSE2,
  kSS,
  kHTT,
  kTM,
  kIA64,
  kPBE,
  kSSE3,
  kPCLMULQDQ,
  kDTES64,
  kMONITOR,
  kDsCpl,
  kVMX,
  kSMX,
  kEST,
  kTM2,
  kSSSE3,
  kCnxtId,
  kSDBG,
  kFMA,
  kCMPXCHG16B,
  kXTPR,
  kPDCM,
  kPCID,
  kDCA,
  kSsE41,
  kSsE42,
  kX2APIC,
  kMOVBE,
  kPOPCNT,
  kTscDeadline,
  kAESNI,
  kXSAVE,
  kOSXSAVE,
  kAVX,
  kF16C,
  kRDRAND,
  kHYPERVISOR,
  kBMI1,
  kAVX2,
  kBMI2,
  kAVX512F,
  kAVX512DQ,
  kRDSEED,
  kADX,
  kAvX512Ifma,
  kCLFLUSHOPT,
  kCLWB,
  kAVX512PF,
  kAVX512ER,
  kAVX512CD,
  kSHA,
  kAVX512BW,
  kAVX512VL,
  kPREFETCHWT1,
  kAvX512Vbmi,
  kWAITPKG,
  kAvX512VbmI2,
  kGFNI,
  kVAES,
  kVPCLMULQDQ,
  kAvX512Vnni,
  kAvX512Bitalg,
  kAvX512Vpopcntdq,
  kRDPID,
  kKL,
  kCLDEMOTE,
  kMOVDIRI,
  kMOVDIR64B,
  kENQCMD,
  kAvX5124Vnniw,
  kAvX5124Fmaps,
  kFSRM,
  kUINTR,
  kAvX512VP2Intersect,
  kSERIALIZE,
  kAmxBF16,
  kAvX512FP16,
  kAmxTile,
  kAmxInT8,
  kLahfLm,
  kLZCNT,
  kSSE4A,
  kPREFETCHW,
  kXOP,
  kFMA4,
  kTBM,
  kSYSCALL,
  kMMXEXT,
  kRDTSCP,
  kLM,
  k3Dnowext,
  k3Dnow,
  kSHA512,
  kSM3,
  kSM4,
  kAvxVnni,
  kAvX512BF16,
  kCMPCCXADD,
  kAmxFP16,
  kAvxIfma,
  kMOVRS,
  kCLZERO,
  kRDPRU,
  kWBNOINVD,
  kPREFETCHI,
  kAvX512Bmm,
  kFP,
  kASIMD,
  kEVTSTRM,
  kAES,
  kPMULL,
  kSHA1,
  kSHA2,
  kCRC32,
  kATOMICS,
  kFPHP,
  kASIMDHP,
  kCPUID,
  kASIMDRDM,
  kJSCVT,
  kFCMA,
  kLRCPC,
  kDCPOP,
  kSHA3,
  kASIMDDP,
  kSVE,
  kASIMDFHM,
  kDIT,
  kUSCAT,
  kILRCPC,
  kFLAGM,
  kSSBS,
  kSB,
  kPACA,
  kPACG,
  kDCPODP,
  kSVE2,
  kSVEAES,
  kSVEPMULL,
  kSVEBITPERM,
  kSVESHA3,
  kSVESM4,
  kFLAGM2,
  kFRINT,
  kSVEI8MM,
  kSVEF32MM,
  kSVEF64MM,
  kSVEBF16,
  kI8MM,
  kBF16,
  kDGH,
  kRNG,
  kBTI,
  kMTE,
  kECV,
  kAFP,
  kRPRES,
  kMTE3,
  kSME,
  kSmeI16I64,
  kSmeF64F64,
  kSmeI8I32,
  kSmeF16F32,
  kSmeB16F32,
  kSmeF32F32,
  kSmeFA64,
  kWFXT,
};

/**
 * @brief 현재 CPU/OS가 지원하는 명령어셋·기능을 런타임에 감지합니다.
 *
 * x86/x64 는 CPUID(leaf 1/7/확장 leaf)의 결과를, ARM 은 HWCAP/HWCAP2
 * (macOS sysctlbyname, Windows IsProcessorFeaturePresent 포함) 조회
 * 결과를 그대로 담아 반환합니다. 그 외 아키텍처는 모든 기능이
 * 미지원인 것으로 취급됩니다.
 *
 * @return 아키텍처별 원시 레지스터 값이 채워진 Register.
 */
Register GetCPUFeatures();

/**
 * @brief 레지스터 값에 특정 CPU 기능이 있는지 확인합니다.
 *
 * x86에서 AVX 이상(YMM/ZMM/TILE 확장 상태를 쓰는 기능)은 CPUID 지원
 * 여부만으로는 부족해, OS가 해당 확장 상태를 활성화했는지(XCR0)까지
 * 추가로 검증합니다.
 *
 * @param cpu_feature_flag GetCPUFeatures() 로 얻은 원시 레지스터 값.
 * @param feature 확인할 기능 식별자.
 * @return 기능이 지원되면 true. 현재 아키텍처 테이블에 없는
 *         기능이거나, x86에서 OS가 필요한 XCR0 상태를 활성화하지
 *         않았다면 false.
 */
bool HasFeature(const Register& cpu_feature_flag, CpuFeature feature);

}  // namespace bedrock::intrinsic
