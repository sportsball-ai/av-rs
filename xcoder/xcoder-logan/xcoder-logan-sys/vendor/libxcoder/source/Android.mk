# Copyright 2013 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH:= $(call my-dir)

#include $(call all-subdir-makefiles)

################################################################################

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    ni_nvme.c \
    ni_device_api_priv.c \
    ni_device_api.c \
    ni_util.c \
    ni_rsrc_priv.cpp \
    ni_rsrc_api.cpp \
    common/ISharedBuffer.cpp \
		
#PRODUCT_COPY_FILES += $(TARGET_OUT)/lib64/libxcoder.so:/usr/local/lib64/
#PRODUCT_COPY_FILES += ni_rsrc_api_android.h:/usr/local/include/
		

LOCAL_C_INCLUDES := \
        $(LOCAL_PATH)/

LOCAL_CFLAGS += -Werror \
                -Wno-missing-field-initializers \
				-Wno-missing-braces \
				-Wno-sign-compare \
				-Wno-return-type \
				-Wno-pointer-arith \
				-Wno-pointer-sign \
				-Wno-enum-conversion \
				-Wno-unused-parameter \
				-Wno-pointer-bool-conversion \
				-Wno-tautological-pointer-compare \
				-Wno-parentheses \
				-Wno-tautological-compare \
				-Wno-absolute-value \
				-Wno-format
				
LOCAL_CFLAGS += -D_ANDROID \
                -DXCODER_IO_RW_ENABLED \
                -D_FILE_OFFSET_BITS=64 \


LOCAL_SHARED_LIBRARIES := \
    libutils libbinder \
	libcutils liblog libcutils \


LOCAL_MODULE := libxcoder
LOCAL_MODULE_TAGS := optional


LOCAL_CLANG := true

include $(BUILD_SHARED_LIBRARY)

################################################################################

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
		ni_rsrc_mon.c
		

LOCAL_C_INCLUDES := \
        $(LOCAL_PATH)/

LOCAL_CFLAGS += -Werror \
                -Wno-missing-field-initializers \
				-Wno-missing-braces \
				-Wno-sign-compare \
				-Wno-return-type \
				-Wno-pointer-arith \
				-Wno-pointer-sign \
				-Wno-enum-conversion \
				-Wno-unused-parameter \
				-Wno-pointer-bool-conversion \
				-Wno-tautological-pointer-compare \
				-Wno-parentheses \
				-Wno-tautological-compare \
				-Wno-absolute-value \
				-Wno-sometimes-uninitialized

LOCAL_CFLAGS += -D_ANDROID \
                -DXCODER_IO_RW_ENABLED \
                -D_FILE_OFFSET_BITS=64

LOCAL_SHARED_LIBRARIES := \
    libutils libbinder \
	libcutils liblog libxcoder \


LOCAL_MODULE := ni_rsrc_mon
LOCAL_MODULE_TAGS := optional


LOCAL_CLANG := true

include $(BUILD_EXECUTABLE)

################################################################################
include $(CLEAR_VARS)
include $(call all-makefiles-under,$(LOCAL_PATH))
