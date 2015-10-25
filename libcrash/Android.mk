# Copyright (C) 2013 The Android Open Source Project
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

include $(CLEAR_VARS)

ifeq ($(TARGET_CPU_SMP),true)
    LOCAL_CFLAGS += -DANDROID_SMP=1
else
    LOCAL_CFLAGS += -DANDROID_SMP=0
endif

########################################################################
# this library works as part of dvm. synchronizing options with Dvm.mk
########################################################################
include dalvik/vm/Dvm.mk

LOCAL_MODULE    := libcrash

LOCAL_SRC_FILES := \
    DbgBridge.cpp \
    DbgBuff.cpp \
    CrashHandler.cpp \
    HeapInfo.cpp \
    CompilerInfo.cpp \
    ThreadInfo.cpp

LOCAL_SHARED_LIBRARIES += libstlport libdvm liblog libcutils libcorkscrew

LOCAL_C_INCLUDES += \
    dalvik/vm/interp \
    external/stlport/stlport \
    bionic \
    bionic/libstdc++/include \
    system

include $(BUILD_SHARED_LIBRARY)
