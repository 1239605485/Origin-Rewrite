LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := OriginRewrite
LOCAL_SRC_FILES := \
    $(wildcard $(LOCAL_PATH)/src/core/*.c) \
    $(wildcard $(LOCAL_PATH)/src/platform/tef/*.c) \
    $(LOCAL_PATH)/src/entry/mod.c \
    $(LOCAL_PATH)/src/platform/bnm/or_bnm_bridge.cpp \
    $(LOCAL_PATH)/third_party/mod-api/tefkernel/tef_api_imp.c \
    $(wildcard $(LOCAL_PATH)/third_party/BNM/src/*.cpp) \
    $(wildcard $(LOCAL_PATH)/third_party/xdl/*.c)
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/include \
    $(LOCAL_PATH)/third_party/mod-api \
    $(LOCAL_PATH)/third_party/BNM/include \
    $(LOCAL_PATH)/third_party/BNM/external/include \
    $(LOCAL_PATH)/third_party/BNM/external \
    $(LOCAL_PATH)/third_party/BNM/external/utf8 \
    $(LOCAL_PATH)/third_party/BNM/src/private \
    $(LOCAL_PATH)/third_party/xdl/include \
    $(LOCAL_PATH)/third_party/xdl
LOCAL_CPPFLAGS := -std=c++20
LOCAL_CFLAGS := -std=c11 -fvisibility=hidden
LOCAL_CPPFLAGS += -fvisibility=hidden -fvisibility-inlines-hidden
LOCAL_CFLAGS += -DORIGINREWRITE_BUILDING=1 -DBUILDING_DLL=1 -DORIGINREWRITE_USE_ANDROID_LOG=1 -DORIGINREWRITE_USE_BNM=1 -DORIGINREWRITE_VERSION=\"0.9.7-tefmanager-feature-enums\"
LOCAL_CPPFLAGS += -DORIGINREWRITE_BUILDING=1 -DBUILDING_DLL=1 -DORIGINREWRITE_USE_ANDROID_LOG=1 -DORIGINREWRITE_USE_BNM=1 -DORIGINREWRITE_VERSION=\"0.9.7-tefmanager-feature-enums\"
LOCAL_LDLIBS := -llog -ldl -lm
LOCAL_LDFLAGS := -Wl,--exclude-libs,ALL
include $(BUILD_SHARED_LIBRARY)
