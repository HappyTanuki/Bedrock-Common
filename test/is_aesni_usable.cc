/**
 * @file is_aesni_usable.cc
 * @brief 실행 CPU/OS가 AES-NI 명령어셋을 지원하는지 확인하는 테스트.
 *
 * bedrock::intrinsic::GetCPUFeatures()로 얻은 레지스터 값에
 * CpuFeature::kAESNI 비트가 서 있는지 HasFeature로 검사한다.
 *
 * @note AES-NI를 지원하지 않는 CPU/OS에서 실행하면 항상 실패(-1)
 *       한다. 이는 버그가 아니라 실행 환경의 하드웨어 제약이다.
 * @return AES-NI를 지원하면 0, 아니면 -1.
 */
#include "common.h"

/** @brief 현재 CPU가 AES-NI를 지원하면 0, 아니면 -1을 반환한다. */
int main() {
  auto feature_flag = bedrock::intrinsic::GetCPUFeatures();

  if (bedrock::intrinsic::HasFeature(feature_flag,
                                     bedrock::intrinsic::CpuFeature::kAESNI)) {
    return 0;
  }
  return -1;
}
