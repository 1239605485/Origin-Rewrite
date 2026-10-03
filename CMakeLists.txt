cmake_minimum_required(VERSION 3.22)
project(OriginRewrite C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CORE
    src/domain/or_lifecycle.c src/domain/or_effects.c src/storage/or_config.c
    src/presentation/or_notice.c src/presentation/or_boss.c src/app/or_app.c)
set(PLATFORM
    src/platform/tef/or_probe.c src/platform/tef/or_read.c src/platform/tef/or_hooks.c
    src/storage/or_log.c src/entry/mod.c mod-api/tefkernel/tef_api_imp.c)
add_library(OriginRewrite SHARED ${CORE} ${PLATFORM})
target_include_directories(OriginRewrite PRIVATE include mod-api)
target_compile_options(OriginRewrite PRIVATE -Wall -Wextra -Werror)
target_link_libraries(OriginRewrite PRIVATE m)
if(ANDROID)
    if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
        message(FATAL_ERROR "This validation build supports Android ARM64 only")
    endif()
    set_target_properties(OriginRewrite PROPERTIES OUTPUT_NAME "OriginRewrite.android.arm64")
else()
    set_target_properties(OriginRewrite PROPERTIES OUTPUT_NAME "OriginRewrite.host")
    enable_testing()
    add_executable(test_core tests/test_core.c ${CORE})
    target_include_directories(test_core PRIVATE include)
    target_link_libraries(test_core PRIVATE m)
    add_test(NAME core_contracts COMMAND test_core)
    add_executable(test_platform tests/test_platform.c ${CORE}
        src/platform/tef/or_probe.c src/platform/tef/or_read.c src/platform/tef/or_hooks.c
        mod-api/tefkernel/tef_api_imp.c)
    target_include_directories(test_platform PRIVATE include mod-api src/platform/tef)
    target_link_libraries(test_platform PRIVATE m)
    add_test(NAME platform_contracts COMMAND test_platform)
endif()
