# Copyright (C) 2006 The Android Open Source Project
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

LOCAL_PATH := $(call my-dir)

WITH_JIT := $(strip $(WITH_JIT))
ifeq ($(WITH_JIT),)
  ifeq ($(TARGET_ARCH),x86)
    WITH_JIT := true

    # For all builds, except for the -user build we will enable support for
    # VTune Amplifier by default. To collect any information about JIT look
    # at -Xjitvtuneinfo:<info> option. By default, -Xjitvtuneinfo:none is
    # used and it doesn't affect JIT compiler (one extra 'if' per compiled
    # trace).
    ifneq ($(TARGET_BUILD_VARIANT),user)
      VTUNE_DALVIK := $(strip $(VTUNE_DALVIK))
      ifeq ($(VTUNE_DALVIK),)
        VTUNE_DALVIK := true
      endif
    endif

  else
    WITH_JIT := false
    WITH_PCG := false
  endif
endif

# Turning on WITH_PCG when WITH_JIT=true (either through command-line
# or setting it to true above), and when WITH_PCG isn't already set
ifeq ($(WITH_JIT),true)
  ifeq ($(TARGET_ARCH),x86)
    ifeq ($(WITH_PCG),)
      ifneq ($(USE_INTEL_IPP),true)
          # PCG depends on libsvml from vendor/intel/
          WITH_PCG := false
      else
          WITH_PCG := true
      endif
    endif
  endif
endif

# Region GC is an optimization in DalvikVM GC to reduce the GC pause time
# and increase the application performance.
WITH_REGION_GC := $(strip $(WITH_REGION_GC))
ifeq ($(WITH_REGION_GC),)
  ifeq ($(TARGET_ARCH),x86)
    WITH_REGION_GC := true
  else
    WITH_REGION_GC := false
  endif
endif

# Disable REGION_GC on small ram devices to prevent memory overhead
ifeq ($(BOARD_HAVE_SMALL_RAM),true)
    WITH_REGION_GC := false
endif

# TLA is an optimization in DalvikVM to provide thread local allocation for small objects.
WITH_TLA := $(strip $(WITH_TLA))
ifeq ($(WITH_TLA),)
  ifeq ($(TARGET_ARCH),x86)
    WITH_TLA := true
  else
    WITH_TLA := false
  endif
endif

# Remove TLA on small ram devices to prevent memory overhead
ifeq ($(BOARD_HAVE_SMALL_RAM),true)
  WITH_TLA := false
endif

# Condional card marking is an optimization in DalvikVM to reduce
# the card dirtying.cache pollution
WITH_CONDMARK := $(strip $(WITH_CONDMARK))
ifeq ($(WITH_CONDMARK),)
  ifeq ($(TARGET_ARCH),x86)
    WITH_CONDMARK := true
  else
    WITH_CONDMARK := false
  endif
endif

subdirs := $(addprefix $(LOCAL_PATH)/,$(addsuffix /Android.mk, \
		libdex \
		vm \
		dexgen \
		dexlist \
		dexopt \
		dexdump \
		dx \
		tools \
		unit-tests \
	))

ifeq ($(TARGET_ARCH),x86)
  subdirs += $(addprefix $(LOCAL_PATH)/,$(addsuffix /Android.mk, libcrash))
  ifeq ($(WITH_JIT),true)
    ifeq ($(WITH_PCG),true)
      subdirs += $(addprefix $(LOCAL_PATH)/,$(addsuffix /Android.mk, \
                 vm/compiler/codegen/x86/pcg \
                 ))
    endif
  endif
endif

include $(subdirs)


.PHONY: dex dex-debug
ifeq ($(DONT_INSTALL_DEX_FILES),true)
dex:
	@echo "Forcing a remake with DONT_INSTALL_DEX_FILES=false"
	$(hide) $(MAKE) DONT_INSTALL_DEX_FILES=false
else
# DONT_INSTALL_DEX_FILES is already false, so a normal make takes care of it.
dex: $(DEFAULT_GOAL)
endif

d :=
ifneq ($(GENERATE_DEX_DEBUG),)
d := debug
endif
ifneq ($(DONT_INSTALL_DEX_FILES),true)
d := $(d)-install
endif
ifneq ($(d),debug-install)
# generate the debug .dex files, with a copy in ./dalvik/DEBUG-FILES.
# We need to rebuild the .dex files for the debug output to be generated.
# The "touch -c $(DX)" is a hack that we know will force
# a rebuild of the .dex files.  If $(DX) doesn't exist yet,
# we won't touch it (-c) and the normal build will create
# the .dex files naturally.
dex-debug:
	@echo "Forcing an app rebuild with GENERATE_DEX_DEBUG=true"
	@touch -c $(DX)
	$(hide) $(MAKE) DONT_INSTALL_DEX_FILES=false GENERATE_DEX_DEBUG=true
else
# GENERATE_DEX_DEBUG and DONT_INSTALL_DEX_FILES are already set properly,
# so a normal make takes care of it.
dex-debug: $(DEFAULT_GOAL)
endif
