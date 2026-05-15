LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := medicaldisplay_ai_jni
LOCAL_SRC_FILES := native_ai_engine.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../ai_engine/include
LOCAL_CPPFLAGS := -std=c++17 -Wall -Wextra -fexceptions -frtti
LOCAL_LDLIBS := -llog -landroid
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := medicaldisplay_display_jni
LOCAL_SRC_FILES := native_display_engine.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../display_engine/include $(LOCAL_PATH)/../../../ai_engine/include
LOCAL_CPPFLAGS := -std=c++17 -Wall -Wextra -fexceptions -frtti
LOCAL_LDLIBS := -llog -landroid
include $(BUILD_SHARED_LIBRARY)
