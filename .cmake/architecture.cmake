# ============================================================
# Architecture detection
# ============================================================
# 타겟 아키텍처를 CMAKE_SYSTEM_PROCESSOR로 판별하여 다음 변수를 설정합니다.
#   BEDROCK_ARCH_X86   : x86/x64면 1, 아니면 0
#   BEDROCK_ARCH_ARM   : ARM(32/64)이면 1, 아니면 0
#   BEDROCK_ARCH_NAME  : "x86" / "arm" / 원시 프로세서명
# 이 값들은 config.h.in -> config.h 로 치환되어 C++ 매크로로 노출됩니다.
#
# CMAKE_SYSTEM_PROCESSOR 예시값:
#   x86 계열 : x86_64(GCC/Clang), AMD64(MSVC), x86, i686, i386
#   ARM 계열 : aarch64, arm64(macOS), ARM64(MSVC), armv7l, arm

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _bedrock_arch_lc)

set(BEDROCK_ARCH_X86 0)
set(BEDROCK_ARCH_ARM 0)

if(_bedrock_arch_lc MATCHES "^(x86_64|amd64|x86|i[3-6]86)$")
    set(BEDROCK_ARCH_X86 1)
    set(BEDROCK_ARCH_NAME "x86")
elseif(_bedrock_arch_lc MATCHES "^(aarch64|arm64|armv[0-9].*|arm)$")
    set(BEDROCK_ARCH_ARM 1)
    set(BEDROCK_ARCH_NAME "arm")
else()
    set(BEDROCK_ARCH_NAME "${CMAKE_SYSTEM_PROCESSOR}")
    message(WARNING
        "Common: 알 수 없는 타겟 아키텍처 '${CMAKE_SYSTEM_PROCESSOR}'. "
        "x86/ARM 어느 쪽도 아니므로 SIMD 감지는 비활성(0)으로 생성됩니다.")
endif()

unset(_bedrock_arch_lc)

message(STATUS
    "Common: target arch = ${BEDROCK_ARCH_NAME} "
    "(x86=${BEDROCK_ARCH_X86}, arm=${BEDROCK_ARCH_ARM}, raw=${CMAKE_SYSTEM_PROCESSOR})")
