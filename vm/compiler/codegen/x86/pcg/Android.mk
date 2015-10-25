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

#
# Android.mk for PCG user plugin
#

PCG_SRC_FILES := \
    Analysis.cpp \
    Client.cpp \
    ChainingCellException.cpp \
    CodeGeneration.cpp \
    CompilationErrorPCG.cpp \
    CompilationUnitPCG.cpp \
    Labels.cpp \
    LowerALU.cpp \
    LowerArray.cpp \
    LowerCall.cpp \
    LowerExtended.cpp \
    LowerGetPut.cpp \
    LowerJump.cpp \
    LowerMemory.cpp \
    LowerOther.cpp \
    PcgInterface.cpp \
    PersistentInfo.cpp \
    Relocation.cpp \
    UtilityPCG.cpp

PCG_C_INCLUDES := \
    dalvik \
    dalvik/vm \
    dalvik/vm/compiler \
    dalvik/vm/compiler/codegen/x86 \
    dalvik/vm/compiler/codegen/x86/lightcg \
    dalvik/vm/compiler/codegen/x86/lightcg/libenc \
    vendor/intel/pcg

PCG_C_INCLUDES_TARGET := \
    $(PCG_C_INCLUDES) \
    external/stlport/stlport \
    bionic

PCG_C_INCLUDES_HOST := \
    $(PCG_C_INCLUDES)

LOCAL_PATH:= $(call my-dir)

#
# Build for the target (device).
#

include $(CLEAR_VARS)

ifeq ($(TARGET_CPU_SMP),true)
    target_smp_flag := -DANDROID_SMP=1
else
    target_smp_flag := -DANDROID_SMP=0
endif
host_smp_flag := -DANDROID_SMP=1

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libpcgdvmjit
LOCAL_CFLAGS += $(target_smp_flag)
LOCAL_CFLAGS += -DARCH_IA32 -DWITH_JIT -DEXTRA_SCRATCH_VR -DMTERP_STUB -Wall -Wextra -O3
ifeq ($(VTUNE_DALVIK),true)
    LOCAL_CFLAGS += -DVTUNE_DALVIK
endif
ifeq ($(INTEL_HOUDINI),true)
    LOCAL_CFLAGS += -DWITH_HOUDINI -DMTERP_NO_UNALIGN_64
endif
ifeq ($(WITH_REGION_GC), true)
    LOCAL_CFLAGS += -DWITH_REGION_GC
endif
ifeq ($(WITH_TLA), true)
    LOCAL_CFLAGS += -DWITH_TLA
endif
ifeq ($(WITH_CONDMARK), true)
    LOCAL_CFLAGS += -DWITH_CONDMARK
endif
TARGET_LIBGCC = $(shell $(TARGET_CC) -m32 -print-libgcc-file-name)
#LOCAL_LDFLAGS += -Wl,--whole-archive $(TARGET_LIBGCC) -Wl,--no-whole-archive
LOCAL_STATIC_LIBRARIES += libpcg libirc_pcg libsvml_pcg
LOCAL_SRC_FILES := $(PCG_SRC_FILES)

LOCAL_SHARED_LIBRARIES += libcutils libdvm libdl libstlport
LOCAL_C_INCLUDES += $(PCG_C_INCLUDES_TARGET)

include $(BUILD_SHARED_LIBRARY)

#
# Build libpcgdvmjit with JIT_TUNING
#

include $(CLEAR_VARS)

ifeq ($(TARGET_CPU_SMP),true)
    target_smp_flag := -DANDROID_SMP=1
else
    target_smp_flag := -DANDROID_SMP=0
endif
host_smp_flag := -DANDROID_SMP=1

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libpcgdvmjit_tuning
LOCAL_CFLAGS += $(target_smp_flag)
LOCAL_CFLAGS += -DARCH_IA32 -DWITH_JIT -DEXTRA_SCRATCH_VR -DMTERP_STUB -Wall -Wextra -O3
ifeq ($(VTUNE_DALVIK),true)
    LOCAL_CFLAGS += -DVTUNE_DALVIK
endif
ifeq ($(INTEL_HOUDINI),true)
    LOCAL_CFLAGS += -DWITH_HOUDINI -DMTERP_NO_UNALIGN_64
endif
ifeq ($(WITH_REGION_GC), true)
    LOCAL_CFLAGS += -DWITH_REGION_GC
endif
ifeq ($(WITH_TLA), true)
    LOCAL_CFLAGS += -DWITH_TLA
endif
ifeq ($(WITH_CONDMARK), true)
    LOCAL_CFLAGS += -DWITH_CONDMARK
endif
LOCAL_CFLAGS += -DWITH_JIT_TUNING
TARGET_LIBGCC = $(shell $(TARGET_CC) -m32 -print-libgcc-file-name)
LOCAL_STATIC_LIBRARIES += libpcg libirc_pcg libsvml_pcg
LOCAL_SRC_FILES := $(PCG_SRC_FILES)
LOCAL_SHARED_LIBRARIES += libcutils libdvm libdl libstlport
LOCAL_C_INCLUDES += $(PCG_C_INCLUDES_TARGET)

include $(BUILD_SHARED_LIBRARY)

#
# Build libpcgdvmjit assert
#

include $(CLEAR_VARS)

ifeq ($(TARGET_CPU_SMP),true)
    target_smp_flag := -DANDROID_SMP=1
else
    target_smp_flag := -DANDROID_SMP=0
endif
host_smp_flag := -DANDROID_SMP=1

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libpcgdvmjit_assert
LOCAL_CFLAGS += $(target_smp_flag)
LOCAL_CFLAGS += -DARCH_IA32 -DWITH_JIT -DEXTRA_SCRATCH_VR -DMTERP_STUB -Wall -Wextra -O3
ifeq ($(VTUNE_DALVIK),true)
    LOCAL_CFLAGS += -DVTUNE_DALVIK
endif
ifeq ($(INTEL_HOUDINI),true)
    LOCAL_CFLAGS += -DWITH_HOUDINI -DMTERP_NO_UNALIGN_64
endif
ifeq ($(WITH_REGION_GC), true)
    LOCAL_CFLAGS += -DWITH_REGION_GC
endif
ifeq ($(WITH_TLA), true)
    LOCAL_CFLAGS += -DWITH_TLA
endif
ifeq ($(WITH_CONDMARK), true)
    LOCAL_CFLAGS += -DWITH_CONDMARK
endif
LOCAL_CFLAGS += -UNDEBUG -DDEBUG=1 -DLOG_NDEBUG=1 -DWITH_DALVIK_ASSERT -DWITH_JIT_TUNING
TARGET_LIBGCC = $(shell $(TARGET_CC) -m32 -print-libgcc-file-name)
LOCAL_STATIC_LIBRARIES += libpcg libirc_pcg libsvml_pcg
LOCAL_SRC_FILES := $(PCG_SRC_FILES)
LOCAL_SHARED_LIBRARIES += libcutils libdvm libdl libstlport
LOCAL_C_INCLUDES += $(PCG_C_INCLUDES_TARGET)

include $(BUILD_SHARED_LIBRARY)

#
# Build for the host.
#

ifeq ($(WITH_HOST_DALVIK),true)
    include $(CLEAR_VARS)

    ifeq ($(TARGET_CPU_SMP),true)
        target_smp_flag := -DANDROID_SMP=1
    else
        target_smp_flag := -DANDROID_SMP=0
    endif
    host_smp_flag := -DANDROID_SMP=1

    LOCAL_MODULE_TAGS := optional
    LOCAL_MODULE := libpcgdvmjit
    LOCAL_CFLAGS += $(host_smp_flag)
    LOCAL_CFLAGS += -DARCH_IA32 -DWITH_JIT -DEXTRA_SCRATCH_VR -DMTERP_STUB
    ifeq ($(VTUNE_DALVIK),true)
        LOCAL_CFLAGS += -DVTUNE_DALVIK
    endif
    ifeq ($(INTEL_HOUDINI),true)
        LOCAL_CFLAGS += -DWITH_HOUDINI -DMTERP_NO_UNALIGN_64
    endif
    ifeq ($(WITH_REGION_GC), true)
        LOCAL_CFLAGS += -DWITH_REGION_GC
    endif
    ifeq ($(WITH_TLA), true)
        LOCAL_CFLAGS += -DWITH_TLA
    endif
    ifeq ($(WITH_CONDMARK), true)
        LOCAL_CFLAGS += -DWITH_CONDMARK
    endif
    TARGET_LIBGCC = $(shell $(TARGET_CC) -m32 -print-libgcc-file-name)
    LOCAL_STATIC_LIBRARIES += libpcg_host libirc_pcg libsvml_pcg
    LOCAL_SHARED_LIBRARIES += libdvm
    LOCAL_LDLIBS += -lpthread -ldl
    LOCAL_SRC_FILES := $(PCG_SRC_FILES)
    LOCAL_C_INCLUDES += $(PCG_C_INCLUDES_HOST)

    include $(BUILD_HOST_SHARED_LIBRARY)
endif
