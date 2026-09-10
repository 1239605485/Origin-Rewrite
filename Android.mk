LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := OriginRewrite
LOCAL_SRC_FILES := \
    $(wildcard src/core/*.c) \
    $(wildcard src/platform/tef/*.c) \
    src/entry/mod.c \
    mod-api/tefkernel/tef_api_imp.c \
    $(wildcard xdl/*.c)
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/include \
    $(LOCAL_PATH)/mod-api \
    $(LOCAL_PATH)/xdl/include \
    $(LOCAL_PATH)/xdl
LOCAL_CFLAGS := -std=c11 -fvisibility=hidden
LOCAL_CFLAGS += -DORIGINREWRITE_BUILDING=1 -DBUILDING_DLL=1 -DORIGINREWRITE_USE_ANDROID_LOG=1 -DORIGINREWRITE_VERSION=\"1.0.29-elite-loot-test-arm64\"
LOCAL_LDLIBS := -llog -ldl -lm
LOCAL_LDFLAGS := -Wl,--exclude-libs,ALL
include $(BUILD_SHARED_LIBRARY)
