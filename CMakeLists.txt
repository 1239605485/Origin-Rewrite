cmake_minimum_required(VERSION 3.22.1)

project("OriginRewrite" LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

set(ORIGINREWRITE_MOD_API_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mod-api"
    CACHE PATH "TEFKernel mod-api directory")

if (NOT EXISTS "${ORIGINREWRITE_MOD_API_DIR}/mod_core.h")
    message(FATAL_ERROR
        "TEFKernel mod-api not found. Set ORIGINREWRITE_MOD_API_DIR to the mod-api directory.")
endif()

set(ORIGINREWRITE_CORE_SOURCES
    src/core/or_ai.c
    src/core/or_config.c
    src/core/or_compat.c
    src/core/or_item_registry.c
    src/core/or_loot.c
    src/core/or_prng.c
    src/core/or_rules.c
    src/core/or_spawn.c
    src/core/or_state.c
    src/core/or_stats.c
    src/core/or_world.c
)

set(ORIGINREWRITE_PLATFORM_SOURCES
    src/entry/mod.c
    src/core/or_death_probe.c
    src/platform/tef/or_log.c
    src/platform/tef/or_config_io.c
    src/platform/tef/or_adapter.c
    src/platform/tef/or_runtime.c
    src/platform/tef/or_broadcast.c
    "${ORIGINREWRITE_MOD_API_DIR}/tefkernel/tef_api_imp.c"
)

add_library(OriginRewrite SHARED
    ${ORIGINREWRITE_CORE_SOURCES}
    ${ORIGINREWRITE_PLATFORM_SOURCES}
)

target_include_directories(OriginRewrite
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${ORIGINREWRITE_MOD_API_DIR}"
)

target_compile_definitions(OriginRewrite PRIVATE
    ORIGINREWRITE_BUILDING=1
    BUILDING_DLL=1
    ORIGINREWRITE_VERSION="0.9.7-tefmanager-feature-enums"
)

if (ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(ORIGINREWRITE_SYSTEM "android")
    # BNM embeds IL2CPP layouts from GlobalSettings.hpp. Fail the build if
    # that selection drifts from Terraria's Unity 2021.3.56f2 runtime.
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/third_party/BNM/include/BNM/UserSettings/GlobalSettings.hpp"
         ORIGINREWRITE_BNM_GLOBAL_SETTINGS)
    string(FIND "${ORIGINREWRITE_BNM_GLOBAL_SETTINGS}" "#define UNITY_VER 213"
           ORIGINREWRITE_BNM_UNITY_2021_MARKER)
    if (ORIGINREWRITE_BNM_UNITY_2021_MARKER EQUAL -1)
        message(FATAL_ERROR "OriginRewrite Android ARM64 requires BNM UNITY_VER 213 for Unity 2021.3.x")
    endif()
    target_sources(OriginRewrite PRIVATE
        src/platform/bnm/or_bnm_bridge.cpp
    )
    add_subdirectory(third_party/BNM EXCLUDE_FROM_ALL)
    add_subdirectory(third_party/xdl EXCLUDE_FROM_ALL)
    set_target_properties(BNM xdl PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_definitions(OriginRewrite PRIVATE
        ORIGINREWRITE_USE_ANDROID_LOG=1
        ORIGINREWRITE_USE_BNM=1
    )
    target_link_libraries(OriginRewrite PRIVATE BNM xdl log dl m)
    target_link_options(OriginRewrite PRIVATE "-Wl,--exclude-libs,ALL")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(ORIGINREWRITE_SYSTEM "windows")
    target_sources(OriginRewrite PRIVATE
        src/platform/bnm/or_bnm_bridge_stub.c
    )
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(ORIGINREWRITE_SYSTEM "darwin")
    target_sources(OriginRewrite PRIVATE
        src/platform/bnm/or_bnm_bridge_stub.c
    )
else()
    set(ORIGINREWRITE_SYSTEM "linux")
    target_sources(OriginRewrite PRIVATE
        src/platform/bnm/or_bnm_bridge_stub.c
    )
    target_link_libraries(OriginRewrite PRIVATE m)
endif()

if (ANDROID_ABI STREQUAL "arm64-v8a" OR CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    set(ORIGINREWRITE_ARCH "arm64")
elseif (ANDROID_ABI STREQUAL "armeabi-v7a" OR CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")
    set(ORIGINREWRITE_ARCH "arm")
elseif (ANDROID_ABI STREQUAL "x86_64" OR CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
    set(ORIGINREWRITE_ARCH "x64")
elseif (ANDROID_ABI STREQUAL "x86" OR CMAKE_ANDROID_ARCH_ABI STREQUAL "x86")
    set(ORIGINREWRITE_ARCH "x86")
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    set(ORIGINREWRITE_ARCH "x64")
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "i[3-6]86|x86")
    set(ORIGINREWRITE_ARCH "x86")
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(ORIGINREWRITE_ARCH "arm64")
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "armv7|armv7a|ARMv7|arm")
    set(ORIGINREWRITE_ARCH "arm")
elseif (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ORIGINREWRITE_ARCH "x64")
else()
    set(ORIGINREWRITE_ARCH "x86")
endif()

if (WIN32)
    set_target_properties(OriginRewrite PROPERTIES PREFIX "" SUFFIX ".dll")
elseif (APPLE)
    set_target_properties(OriginRewrite PROPERTIES SUFFIX ".dylib")
else()
    set_target_properties(OriginRewrite PROPERTIES SUFFIX ".so")
endif()

set_target_properties(OriginRewrite PROPERTIES
    OUTPUT_NAME "OriginRewrite.${ORIGINREWRITE_SYSTEM}.${ORIGINREWRITE_ARCH}"
    POSITION_INDEPENDENT_CODE ON
)

option(ORIGINREWRITE_BUILD_TESTS "Build pure-core tests" ON)

if (ORIGINREWRITE_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
    enable_testing()
    add_executable(originrewrite_core_test
        tests/core_test.c
        ${ORIGINREWRITE_CORE_SOURCES}
        src/platform/tef/or_config_io.c
    )
    target_include_directories(originrewrite_core_test
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    if (UNIX AND NOT APPLE)
        target_link_libraries(originrewrite_core_test PRIVATE m)
    endif()
    add_test(NAME originrewrite_core_test COMMAND originrewrite_core_test)
endif()
