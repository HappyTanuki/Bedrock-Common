#include "common.h"

int main() {
  auto feature_flag = bedrock::intrinsic::GetCPUFeatures();

  if (bedrock::intrinsic::HasFeature(feature_flag,
                                     bedrock::intrinsic::CpuFeature::kAESNI)) {
    return 0;
  }
  return -1;
}
