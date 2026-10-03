LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := OriginRewrite
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include $(LOCAL_PATH)/mod-api
LOCAL_CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror
LOCAL_SRC_FILES := src/domain/or_lifecycle.c src/domain/or_effects.c \
    src/storage/or_config.c src/storage/or_log.c src/app/or_app.c \
    src/presentation/or_notice.c src/presentation/or_boss.c \
    src/platform/tef/or_probe.c src/platform/tef/or_read.c src/platform/tef/or_hooks.c \
    src/entry/mod.c mod-api/tefkernel/tef_api_imp.c
LOCAL_LDLIBS := -lm
include $(BUILD_SHARED_LIBRARY)
