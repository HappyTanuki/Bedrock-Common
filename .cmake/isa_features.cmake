# ============================================================
# ISA(instruction set) 활성화 플래그 ── native + 런타임 인식 모델
# ============================================================
# 전제: 실행할 그 머신에서 직접 빌드한다(소스 기반 배포처럼).
#   - -march=native(x86) / -mcpu=native(ARM) / /arch(MSVC) 로 빌드 머신의
#     명령어셋을 컴파일러가 모두 켠다 → 그 CPU가 가진 인트린식은 그대로 컴파일됨.
#   - "이 CPU가 X를 지원하나"의 판단은 전적으로 런타임(GetCPUFeatures/
#     IsCpuEnabledFeature, CPUID/HWCAP 실측)이 담당한다. 컴파일타임 매크로는 두지 않는다.
#     (컴파일러 사전정의 매크로나 configure 프로브는 부정확/취약하므로 쓰지 않음.)
#
# 선행 조건: architecture.cmake(BEDROCK_ARCH_X86/ARM) 와 타겟 생성 이후 include.

option(BEDROCK_NATIVE_ISA "빌드 머신이 지원하는 명령어셋을 모두 활성화" ON)
set(BEDROCK_MSVC_ARCH "AVX2" CACHE STRING "MSVC /arch 레벨 (AVX512/AVX2/AVX 또는 빈 값)")

set(BEDROCK_ISA_FLAGS "")
if(BEDROCK_NATIVE_ISA)
    if(BEDROCK_ARCH_X86)
        if(MSVC)
            if(NOT BEDROCK_MSVC_ARCH STREQUAL "")
                set(BEDROCK_ISA_FLAGS "/arch:${BEDROCK_MSVC_ARCH}")
            endif()
        else()
            set(BEDROCK_ISA_FLAGS "-march=native")
        endif()
    elseif(BEDROCK_ARCH_ARM AND NOT MSVC)
        set(BEDROCK_ISA_FLAGS "-mcpu=native")
    endif()
endif()

# PUBLIC: 라이브러리 사용자 코드도 같은 명령어셋으로 컴파일되도록 전이.
if(BEDROCK_ISA_FLAGS AND TARGET ${SUB_PROJECT_NAME})
    target_compile_options(${SUB_PROJECT_NAME} PUBLIC ${BEDROCK_ISA_FLAGS})
endif()

message(STATUS "Common: ISA flags='${BEDROCK_ISA_FLAGS}' (feature 판단은 런타임 CPUID/HWCAP)")
