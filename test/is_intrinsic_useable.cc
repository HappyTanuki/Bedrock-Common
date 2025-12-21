#include "common.h"

int main() {
  auto feature_flag = bedrock::intrinsic::GetCPUFeatures();

  if (bedrock::intrinsic::IsCpuEnabledFeature(feature_flag, "AESNI")) {
    return 0;
  } else {
    return -1;
  }
}
