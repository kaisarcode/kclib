## Makefile
## Summary: Cross-compilation builder for llm.
##
## Author:  KaisarCode
## Website: https://kaisarcode.com
## License: https://www.gnu.org/licenses/gpl-3.0.html

ANDROID_HOME  ?= $(HOME)/.local/share/android-sdk
NDK_VERSION   ?= 27.2.12479018
NDK_DIR       := $(ANDROID_HOME)/ndk/$(NDK_VERSION)
NDK_TOOLCHAIN := $(NDK_DIR)/build/cmake/android.toolchain.cmake
XDG_DATA_HOME ?= $(HOME)/.local/share
OSXCROSS_ROOT ?= $(XDG_DATA_HOME)/osxcross/target
MACOSX_DEPLOYMENT_TARGET ?= 11.0
IOS_DEPLOYMENT_TARGET ?= 13.0
DETECTED_MACOSX_SDK := $(shell ls -d "$(OSXCROSS_ROOT)"/SDK/MacOSX*.sdk 2>/dev/null | sort -V | tail -n 1)
DETECTED_IPHONEOS_SDK := $(shell ls -d "$(OSXCROSS_ROOT)"/SDK/iPhoneOS*.sdk 2>/dev/null | sort -V | tail -n 1)
DETECTED_IPHONESIMULATOR_SDK := $(shell ls -d "$(OSXCROSS_ROOT)"/SDK/iPhoneSimulator*.sdk 2>/dev/null | sort -V | tail -n 1)
MACOSX_SDK ?= $(DETECTED_MACOSX_SDK)
IPHONEOS_SDK ?= $(DETECTED_IPHONEOS_SDK)
IPHONESIMULATOR_SDK ?= $(DETECTED_IPHONESIMULATOR_SDK)
ifeq ($(origin MACOSX_SDK), environment)
override MACOSX_SDK := $(DETECTED_MACOSX_SDK)
endif
ifeq ($(strip $(MACOSX_SDK)),)
override MACOSX_SDK := $(DETECTED_MACOSX_SDK)
endif
ifneq ($(origin MACOSX_SDK), command line)
ifeq ($(wildcard $(MACOSX_SDK)),)
override MACOSX_SDK := $(DETECTED_MACOSX_SDK)
endif
endif
ifeq ($(origin IPHONEOS_SDK), environment)
override IPHONEOS_SDK := $(DETECTED_IPHONEOS_SDK)
endif
ifeq ($(strip $(IPHONEOS_SDK)),)
override IPHONEOS_SDK := $(DETECTED_IPHONEOS_SDK)
endif
ifneq ($(origin IPHONEOS_SDK), command line)
ifeq ($(wildcard $(IPHONEOS_SDK)),)
override IPHONEOS_SDK := $(DETECTED_IPHONEOS_SDK)
endif
endif
ifeq ($(origin IPHONESIMULATOR_SDK), environment)
override IPHONESIMULATOR_SDK := $(DETECTED_IPHONESIMULATOR_SDK)
endif
ifeq ($(strip $(IPHONESIMULATOR_SDK)),)
override IPHONESIMULATOR_SDK := $(DETECTED_IPHONESIMULATOR_SDK)
endif
ifneq ($(origin IPHONESIMULATOR_SDK), command line)
ifeq ($(wildcard $(IPHONESIMULATOR_SDK)),)
override IPHONESIMULATOR_SDK := $(DETECTED_IPHONESIMULATOR_SDK)
endif
endif
OSXCROSS_X86_64_CC := $(OSXCROSS_ROOT)/bin/o64-clang
OSXCROSS_X86_64_CXX := $(OSXCROSS_ROOT)/bin/o64-clang++
OSXCROSS_AARCH64_CC := $(OSXCROSS_ROOT)/bin/oa64-clang
OSXCROSS_AARCH64_CXX := $(OSXCROSS_ROOT)/bin/oa64-clang++
OSXCROSS_IOS_AARCH64_CC := $(OSXCROSS_ROOT)/bin/ios64-clang
OSXCROSS_IOS_AARCH64_CXX := $(OSXCROSS_ROOT)/bin/ios64-clang++
OSXCROSS_IOSSIM_AARCH64_CC := $(OSXCROSS_ROOT)/bin/iossim64-clang
OSXCROSS_IOSSIM_AARCH64_CXX := $(OSXCROSS_ROOT)/bin/iossim64-clang++
OSXCROSS_IOSSIM_X86_64_CC := $(OSXCROSS_ROOT)/bin/iossimx64-clang
OSXCROSS_IOSSIM_X86_64_CXX := $(OSXCROSS_ROOT)/bin/iossimx64-clang++
OSXCROSS_INSTALL_NAME_TOOL ?= $(firstword $(wildcard $(OSXCROSS_ROOT)/bin/*-install_name_tool))
OSXCROSS_X86_64_AR := $(or $(firstword $(wildcard $(OSXCROSS_ROOT)/bin/x86_64-apple-darwin25.1-ar)),$(firstword $(wildcard $(OSXCROSS_ROOT)/bin/x86_64-apple-darwin*-ar)))
OSXCROSS_X86_64_RANLIB := $(or $(firstword $(wildcard $(OSXCROSS_ROOT)/bin/x86_64-apple-darwin25.1-ranlib)),$(firstword $(wildcard $(OSXCROSS_ROOT)/bin/x86_64-apple-darwin*-ranlib)))
OSXCROSS_AARCH64_AR := $(or $(firstword $(wildcard $(OSXCROSS_ROOT)/bin/aarch64-apple-darwin25.1-ar)),$(firstword $(wildcard $(OSXCROSS_ROOT)/bin/aarch64-apple-darwin*-ar)))
OSXCROSS_AARCH64_RANLIB := $(or $(firstword $(wildcard $(OSXCROSS_ROOT)/bin/aarch64-apple-darwin25.1-ranlib)),$(firstword $(wildcard $(OSXCROSS_ROOT)/bin/aarch64-apple-darwin*-ranlib)))
WINE ?= wine
WINE_X86_64_CC ?= x86_64-w64-mingw32-gcc
WINE_X86_64_CXX ?= x86_64-w64-mingw32-g++

WASM_EMCMAKE ?= emcmake
WASM_EMCC    ?= emcc
WASM_NODE    ?= node

# CUDA cross-compilation support for aarch64 from x86_64.
CUDA_HOST_CXX   ?= $(firstword $(wildcard /usr/bin/aarch64-linux-gnu-g++-13 /usr/bin/aarch64-linux-gnu-g++-12 /usr/bin/aarch64-linux-gnu-g++-11 /usr/bin/aarch64-linux-gnu-g++))
CROSS_CUDA_ROOT ?= $(firstword $(wildcard /opt/cuda-sbsa/cuda-12.4))
CUDA_CROSS_NVCC ?= $(XDG_DATA_HOME)/kaisarcode/setup/bin/nvcc-aarch64-wrapper

KC_CMAKE_NOISE := -e '^-- ' \
                  -e '^CMAKE_BUILD_TYPE=' \
                  -e 'Unknown CPU architecture\.  Falling back to generic implementations\.' \
                  -e 'CMake Warning at .*/ggml-cpu/CMakeLists.txt:[0-9]*' \
                  -e 'Call Stack (most recent call first):' \
                  -e 'ggml_add_cpu_backend_variant_impl'

KC_LLAMACPP_DIR   ?= $(error KC_LLAMACPP_DIR is not set; export it or pass it to make)
# Resolve to an absolute path: keep absolute paths as-is, prepend CURDIR for relative.
KC_LLAMACPP_DIR_FULL := $(if $(filter /%,$(KC_LLAMACPP_DIR)),$(KC_LLAMACPP_DIR),$(CURDIR)/$(KC_LLAMACPP_DIR))
CUDA              ?= 0
KC_LLM_TEST_MODEL ?= $(CURDIR)/lib/model.gguf
KC_LLM_TEST_LORA  ?= $(CURDIR)/lib/lora.gguf

BUILD_DIR := .build
BIN_DIR   := bin
BUILD_VERSION ?= $(shell find src/ -maxdepth 1 -type f \( -name '*.c' -o -name '*.h' \) -printf '%T@\n' 2>/dev/null | sort -rn | head -1 | cut -d. -f1)
APPLE_BUILD_TAG := -xc

LLAMACPP_COMMIT := $(shell if [ -d "$(KC_LLAMACPP_DIR)/.git" ]; then git -C "$(KC_LLAMACPP_DIR)" rev-parse --short HEAD 2>/dev/null; fi)
ifeq ($(LLAMACPP_COMMIT),)
LLAMACPP_COMMIT := unknown
endif

CUDA_CAPABLE_ARCHS := x86_64 aarch64

cuda_subdir = $(if $(and $(filter 1,$(CUDA)),$(filter $(1),$(CUDA_CAPABLE_ARCHS))),/cuda)
cuda_build  = $(if $(and $(filter 1,$(CUDA)),$(filter $(1),$(CUDA_CAPABLE_ARCHS))),-cuda)
cuda_enabled = $(and $(filter 1,$(CUDA)),$(filter $(1),$(CUDA_CAPABLE_ARCHS)),$(or $(filter $(NATIVE_ARCH),$(1)),$(CROSS_CUDA_ROOT)))
cuda_warn    = $(and $(filter 1,$(CUDA)),$(if $(filter $(1),$(CUDA_CAPABLE_ARCHS)),,warn))
cuda_cross   = $(and $(call cuda_enabled,$(1)),$(filter-out $(NATIVE_ARCH),$(1)),$(CROSS_CUDA_ROOT))
cuda_cross_env = $(if $(call cuda_cross,$(1)),CUDAHOSTCXX=$(CUDA_HOST_CXX))
cuda_cross_flags = $(if $(call cuda_cross,$(1)),-DCUDAToolkit_ROOT=$(CROSS_CUDA_ROOT) -DCMAKE_CUDA_COMPILER=$(CUDA_CROSS_NVCC))
cuda_cross_unset = $(if $(call cuda_cross,$(1)),-U CMAKE_CUDA_COMPILER -U CMAKE_CUDA_COMPILER_TOOLKIT_ROOT -U CUDAToolkit_* -U CUDA_* -U GGML_AVX -U GGML_AVX2 -U GGML_BMI2 -U GGML_F16C -U GGML_FMA -U GGML_SSE42)

define cmake_build
	@ninja="$(1)/build.ninja"; \
	if [ ! -f "$$ninja" ]; then \
		echo "No build.ninja found in $(1)" >&2; \
		exit 1; \
	fi; \
	prelog=$$(mktemp); \
	if ! $(2) ninja -C $(1) -n > "$$prelog" 2>&1; then \
		cat "$$prelog"; \
		rm -f "$$prelog"; \
		exit 1; \
	fi; \
	if grep -q "ninja: no work to do." "$$prelog"; then \
		rm -f "$$prelog"; \
		out=$$(mktemp); \
		$(2) cmake --build $(1) 2>"$$out"; \
		r=$$?; \
		if [ -s "$$out" ]; then grep -v -e 'skipping incompatible' -e 'has no symbols' $(KC_CMAKE_NOISE) < "$$out" | sed '/^[[:space:]]*$$/d'; fi; \
		rm -f "$$out"; \
		exit $$r; \
	fi; \
	rm -f "$$prelog"; \
	out=$$(mktemp); \
	$(2) ninja -C $(1) 2>"$$out"; \
	r=$$?; \
	if [ -s "$$out" ]; then grep -v -e 'skipping incompatible' -e 'has no symbols' $(KC_CMAKE_NOISE) < "$$out" | sed '/^[[:space:]]*$$/d'; fi; \
	rm -f "$$out"; \
	if [ $$r -ne 0 ]; then \
		exit 1; \
	fi; \
	:
endef

define cmake_configure
	@out=$$(mktemp); \
	if ! $(1) >"$$out" 2>&1; then \
		grep -v -e 'skipping incompatible' -e 'has no symbols' $(KC_CMAKE_NOISE) < "$$out"; \
		rm -f "$$out"; \
		exit 1; \
	fi; \
	grep -v -e 'skipping incompatible' -e 'has no symbols' $(KC_CMAKE_NOISE) < "$$out"; \
	rm -f "$$out"
endef

HOST_ARCH       := $(shell uname -m)
HOST_SYSTEM     := $(shell uname -s)
NATIVE_ARCH     := unsupported
NATIVE_PLATFORM := unsupported

ifneq ($(filter x86_64 amd64,$(HOST_ARCH)),)
NATIVE_ARCH := x86_64
endif

ifneq ($(filter i386 i686,$(HOST_ARCH)),)
NATIVE_ARCH := i686
endif

ifneq ($(filter aarch64 arm64,$(HOST_ARCH)),)
NATIVE_ARCH := aarch64
endif

ifneq ($(filter armv7l armv7%,$(HOST_ARCH)),)
NATIVE_ARCH := armv7
endif

ifneq ($(filter ppc64le powerpc64le,$(HOST_ARCH)),)
NATIVE_ARCH := powerpc64le
endif

ifneq ($(filter riscv64 s390x loongarch64 mips64el mipsel mips,$(HOST_ARCH)),)
NATIVE_ARCH := $(HOST_ARCH)
endif

ifeq ($(HOST_SYSTEM),Linux)
NATIVE_PLATFORM := linux
endif

ifneq ($(filter MINGW% MSYS% CYGWIN%,$(HOST_SYSTEM)),)
NATIVE_PLATFORM := windows
endif

NATIVE_TARGET := $(NATIVE_ARCH)/$(NATIVE_PLATFORM)
NATIVE_EXE_EXT :=
NATIVE_STATIC_NAME := libllm.a
NATIVE_SHARED_NAME := libllm.so
NATIVE_IMPORT_LIBRARY :=

ifeq ($(NATIVE_PLATFORM),windows)
NATIVE_EXE_EXT := .exe
NATIVE_SHARED_NAME := libllm.dll
NATIVE_IMPORT_LIBRARY := -DLLM_TEST_IMPORT_LIBRARY=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/libllm.dll.a
endif

ifeq ($(NATIVE_PLATFORM),macos)
NATIVE_SHARED_NAME := libllm.dylib
endif

.DEFAULT_GOAL := $(NATIVE_TARGET)

.PHONY: all test wine wasm wasm32/wasm clean print-config \
	x86_64/linux x86_64/windows x86_64/macos \
	x86_64/iossim \
	i686/linux i686/windows \
	aarch64/linux aarch64/android aarch64/macos aarch64/ios aarch64/iossim \
	armv7/linux armv7/android \
	armv7hf/linux \
	riscv64/linux \
	powerpc64le/linux \
	mips/linux mipsel/linux mips64el/linux \
	s390x/linux \
	loongarch64/linux

print-config:
	@echo "llama.cpp source: $(KC_LLAMACPP_DIR)"
	@echo "vision:           built-in"
	@echo "cuda:             $(CUDA)"
	@echo "build dir:        $(BUILD_DIR)"
	@echo "bin dir:          $(BIN_DIR)"
	@echo "native target:    $(NATIVE_TARGET)"

all: $(if $(filter 1,$(CUDA)),\
	x86_64/linux x86_64/windows x86_64/macos \
	x86_64/iossim \
	aarch64/linux aarch64/android aarch64/macos aarch64/ios aarch64/iossim,\
	x86_64/linux x86_64/windows x86_64/macos \
	x86_64/iossim \
	i686/linux i686/windows \
	aarch64/linux aarch64/android aarch64/macos aarch64/ios aarch64/iossim \
	armv7/linux armv7/android \
	armv7hf/linux \
	riscv64/linux \
	powerpc64le/linux \
	mips/linux mipsel/linux mips64el/linux \
	s390x/linux \
	loongarch64/linux \
	wasm32/wasm)

## Linux

define linux_target
	@mkdir -p $(BIN_DIR)/$(1)/linux$(cuda_subdir)
	@if [ -n "$(call cuda_cross,$(1))" ]; then \
		mkdir -p "$(dir $(CUDA_CROSS_NVCC))"; \
		if [ ! -x "$(CUDA_CROSS_NVCC)" ]; then \
			printf '#!/bin/bash\nexec /usr/bin/nvcc -Xcompiler "-U__GNUC__" -Xcompiler "-D__GNUC__=8" -L"%s/targets/sbsa-linux/lib" "$$@"\n' "$(CROSS_CUDA_ROOT)" > "$(CUDA_CROSS_NVCC)"; \
			chmod +x "$(CUDA_CROSS_NVCC)"; \
		fi; \
	fi
	@cache=$(BUILD_DIR)/$(subst /,-,$(1))-linux$(cuda_build)/CMakeCache.txt; \
	if [ -f "$$cache" ]; then \
		cached_dir=$$(grep -E '^CMAKE_HOME_DIRECTORY:INTERNAL=' "$$cache" 2>/dev/null | cut -d= -f2); \
		build=$${cache%/CMakeCache.txt}/build.ninja; \
		if [ "$$cached_dir" != "$(CURDIR)" ] || [ ! -f "$$build" ]; then \
			rm -f "$$cache" $(BUILD_DIR)/$(subst /,-,$(1))-linux$(cuda_build)/build.ninja && rm -rf $(BUILD_DIR)/$(subst /,-,$(1))-linux$(cuda_build)/CMakeFiles; \
		fi; \
	fi
	@if [ ! -f $(BUILD_DIR)/$(subst /,-,$(1))-linux$(cuda_build)/CMakeCache.txt ]; then \
		builddir="$(BUILD_DIR)/$(subst /,-,$(1))-linux$(cuda_build)"; \
		mkdir -p "$$builddir"; \
		printf '#define KC_LLM_BUILD_VERSION %s\n#define KC_LLAMACPP_COMMIT "%s"\n' "$(BUILD_VERSION)" "$(LLAMACPP_COMMIT)" > "$$builddir/kc_llm_version.h"; \
		out=$$(mktemp); \
		if ! $(call cuda_cross_env,$(1)) cmake -S . -B "$$builddir" $(call cuda_cross_unset,$(1)) \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Linux \
			-DCMAKE_SYSTEM_PROCESSOR=$(2) \
			-DKC_LLAMACPP_DIR=$(KC_LLAMACPP_DIR_FULL) \
			-DLLM_CUDA_ENABLED=$(if $(call cuda_enabled,$(1)),ON,OFF) \
			-DCMAKE_C_COMPILER=$(3) \
			-DCMAKE_CXX_COMPILER=$(4) \
			$(call cuda_cross_flags,$(1)) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$$builddir/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$$builddir/out \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$$builddir/out \
			-G Ninja -Wno-dev >"$$out" 2>&1; then \
			grep -v -e 'skipping incompatible' -e 'has no symbols' $(KC_CMAKE_NOISE) < "$$out"; \
			rm -f "$$out"; \
			exit 1; \
		fi; \
		grep -v -e 'skipping incompatible' -e 'has no symbols' $(KC_CMAKE_NOISE) < "$$out"; \
		rm -f "$$out"; \
	fi
	@builddir="$(BUILD_DIR)/$(subst /,-,$(1))-linux$(cuda_build)"; \
	ver="#define KC_LLM_BUILD_VERSION $(BUILD_VERSION)\n#define KC_LLAMACPP_COMMIT \"$(LLAMACPP_COMMIT)\"\n"; \
	printf "$$ver" > "$$builddir/kc_llm_version.tmp"; \
	if ! diff -q "$$builddir/kc_llm_version.tmp" "$$builddir/kc_llm_version.h" > /dev/null 2>&1; then \
		mv "$$builddir/kc_llm_version.tmp" "$$builddir/kc_llm_version.h"; \
	else \
		rm -f "$$builddir/kc_llm_version.tmp"; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(subst /,-,$(1))-linux$(cuda_build),$(call cuda_cross_env,$(1)))
	@cp $(BUILD_DIR)/$(subst /,-,$(1))-linux$(cuda_build)/out/llm $(BIN_DIR)/$(1)/linux$(cuda_subdir)/llm
	@cp $(BUILD_DIR)/$(subst /,-,$(1))-linux$(cuda_build)/out/merged/libllm.a $(BIN_DIR)/$(1)/linux$(cuda_subdir)/libllm.a
	@cp $(BUILD_DIR)/$(subst /,-,$(1))-linux$(cuda_build)/out/libllm.so $(BIN_DIR)/$(1)/linux$(cuda_subdir)/libllm.so
	@echo "OK $(1)/linux$(cuda_subdir)"
endef

x86_64/linux:
	$(call linux_target,x86_64,x86_64,gcc,g++)

i686/linux:
	$(call linux_target,i686,i686,i686-linux-gnu-gcc,i686-linux-gnu-g++)

aarch64/linux:
	$(call linux_target,aarch64,aarch64,aarch64-linux-gnu-gcc,aarch64-linux-gnu-g++)

armv7hf/linux:
	$(call linux_target,armv7hf,armv7,arm-linux-gnueabihf-gcc,arm-linux-gnueabihf-g++)

armv7/linux:
	$(call linux_target,armv7,armv7,arm-linux-gnueabi-gcc,arm-linux-gnueabi-g++)

riscv64/linux:
	$(call linux_target,riscv64,riscv64,riscv64-linux-gnu-gcc,riscv64-linux-gnu-g++)

powerpc64le/linux:
	$(call linux_target,powerpc64le,powerpc64le,powerpc64le-linux-gnu-gcc,powerpc64le-linux-gnu-g++)

mips/linux:
	$(call linux_target,mips,mips,mips-linux-gnu-gcc,mips-linux-gnu-g++)

mipsel/linux:
	$(call linux_target,mipsel,mips,mipsel-linux-gnu-gcc,mipsel-linux-gnu-g++)

mips64el/linux:
	$(call linux_target,mips64el,mips64,mips64el-linux-gnuabi64-gcc,mips64el-linux-gnuabi64-g++)

s390x/linux:
	$(call linux_target,s390x,s390x,s390x-linux-gnu-gcc,s390x-linux-gnu-g++)

loongarch64/linux:
	$(call linux_target,loongarch64,loongarch64,loongarch64-linux-gnu-gcc,loongarch64-linux-gnu-g++)

## Windows

define windows_target
	@mkdir -p $(BIN_DIR)/$(1)/windows
	@cache=$(BUILD_DIR)/$(1)-windows/CMakeCache.txt; \
	if [ -f "$$cache" ]; then \
		cached_dir=$$(grep -E '^CMAKE_HOME_DIRECTORY:INTERNAL=' "$$cache" 2>/dev/null | cut -d= -f2); \
		build=$${cache%/CMakeCache.txt}/build.ninja; \
		if [ "$$cached_dir" != "$(CURDIR)" ] || [ ! -f "$$build" ]; then \
			rm -f "$$cache" $(BUILD_DIR)/$(1)-windows/build.ninja && rm -rf $(BUILD_DIR)/$(1)-windows/CMakeFiles; \
		fi; \
	fi
	@if [ ! -f $(BUILD_DIR)/$(1)-windows/CMakeCache.txt ]; then \
		builddir="$(BUILD_DIR)/$(1)-windows"; \
		mkdir -p "$$builddir"; \
		printf '#define KC_LLM_BUILD_VERSION %s\n#define KC_LLAMACPP_COMMIT "%s"\n' "$(BUILD_VERSION)" "$(LLAMACPP_COMMIT)" > "$$builddir/kc_llm_version.h"; \
		cmake -S . -B "$$builddir" \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Windows \
			-DCMAKE_SYSTEM_PROCESSOR=$(2) \
			-DKC_LLAMACPP_DIR=$(KC_LLAMACPP_DIR_FULL) \
			-DLLM_CUDA_ENABLED=OFF \
			-DCMAKE_C_COMPILER=$(3) \
			-DCMAKE_CXX_COMPILER=$(4) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$$builddir/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$$builddir/out \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$$builddir/out \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	@builddir="$(BUILD_DIR)/$(1)-windows"; \
	ver="#define KC_LLM_BUILD_VERSION $(BUILD_VERSION)\n#define KC_LLAMACPP_COMMIT \"$(LLAMACPP_COMMIT)\"\n"; \
	printf "$$ver" > "$$builddir/kc_llm_version.tmp"; \
	if ! diff -q "$$builddir/kc_llm_version.tmp" "$$builddir/kc_llm_version.h" > /dev/null 2>&1; then \
		mv "$$builddir/kc_llm_version.tmp" "$$builddir/kc_llm_version.h"; \
	else \
		rm -f "$$builddir/kc_llm_version.tmp"; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-windows)
	@cp $(BUILD_DIR)/$(1)-windows/out/llm.exe $(BIN_DIR)/$(1)/windows/llm.exe
	@cp $(BUILD_DIR)/$(1)-windows/out/libllm.dll $(BIN_DIR)/$(1)/windows/libllm.dll
	@cp $(BUILD_DIR)/$(1)-windows/out/merged/libllm.a $(BIN_DIR)/$(1)/windows/libllm.a
	@cp $(BUILD_DIR)/$(1)-windows/out/libllm.dll.a $(BIN_DIR)/$(1)/windows/libllm.dll.a
	@echo "OK $(1)/windows"
endef

x86_64/windows:
	$(call windows_target,x86_64,x86_64,$(WINE_X86_64_CC),$(WINE_X86_64_CXX))

i686/windows:
	$(call windows_target,i686,i686,i686-w64-mingw32-gcc,i686-w64-mingw32-g++)

## macOS

define macos_target
	@mkdir -p $(BIN_DIR)/$(1)/macos
	@if ! $(2) --version > /dev/null 2>&1; then \
		echo "Missing macOS cross-compiler wrapper: $(2)" >&2; \
		echo "Set OSXCROSS_ROOT to your osxcross target dir and ensure the wrappers are built." >&2; \
		exit 1; \
	fi
	@if [ -z "$(OSXCROSS_INSTALL_NAME_TOOL)" ] || [ ! -x "$(OSXCROSS_INSTALL_NAME_TOOL)" ]; then \
		echo "Missing osxcross install_name_tool under $(OSXCROSS_ROOT)/bin" >&2; \
		exit 1; \
	fi
	@if [ -z "$(4)" ] || [ ! -x "$(4)" ] || [ -z "$(5)" ] || [ ! -x "$(5)" ]; then \
		echo "Missing osxcross ar/ranlib for target $(1) under $(OSXCROSS_ROOT)/bin" >&2; \
		exit 1; \
	fi
	@cache=$(BUILD_DIR)/$(1)-macos$(APPLE_BUILD_TAG)/CMakeCache.txt && \
	if [ -f "$$cache" ] && { ! grep -q '^CMAKE_C_COMPILER:.*=$(2)$$' "$$cache"; }; then \
		rm -f "$$cache" $(BUILD_DIR)/$(1)-macos$(APPLE_BUILD_TAG)/build.ninja && rm -rf $(BUILD_DIR)/$(1)-macos$(APPLE_BUILD_TAG)/CMakeFiles; \
	fi
	@if [ ! -f $(BUILD_DIR)/$(1)-macos$(APPLE_BUILD_TAG)/build.ninja ]; then \
		builddir="$(BUILD_DIR)/$(1)-macos$(APPLE_BUILD_TAG)"; \
		mkdir -p "$$builddir"; \
		printf '#define KC_LLM_BUILD_VERSION %s\n#define KC_LLAMACPP_COMMIT "%s"\n' "$(BUILD_VERSION)" "$(LLAMACPP_COMMIT)" > "$$builddir/kc_llm_version.h"; \
		LD_LIBRARY_PATH="$(OSXCROSS_ROOT)/lib:$${LD_LIBRARY_PATH:-}" PATH="$(OSXCROSS_ROOT)/bin:$$PATH" cmake -S . -B "$$builddir" \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Darwin \
			-DCMAKE_SYSTEM_PROCESSOR=$(1) \
			-DCMAKE_OSX_ARCHITECTURES=$(6) \
			-DCMAKE_OSX_DEPLOYMENT_TARGET=$(MACOSX_DEPLOYMENT_TARGET) \
			-DCMAKE_INSTALL_NAME_TOOL=$(OSXCROSS_INSTALL_NAME_TOOL) \
			-DCMAKE_AR=$(4) \
			-DCMAKE_RANLIB=$(5) \
			-DKC_LLAMACPP_DIR=$(KC_LLAMACPP_DIR_FULL) \
			-DLLM_CUDA_ENABLED=OFF \
			-DCMAKE_C_COMPILER=$(2) \
			-DCMAKE_CXX_COMPILER=$(3) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$$builddir/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$$builddir/out \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$$builddir/out \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	@builddir="$(BUILD_DIR)/$(1)-macos$(APPLE_BUILD_TAG)"; \
	ver="#define KC_LLM_BUILD_VERSION $(BUILD_VERSION)\n#define KC_LLAMACPP_COMMIT \"$(LLAMACPP_COMMIT)\"\n"; \
	printf "$$ver" > "$$builddir/kc_llm_version.tmp"; \
	if ! diff -q "$$builddir/kc_llm_version.tmp" "$$builddir/kc_llm_version.h" > /dev/null 2>&1; then \
		mv "$$builddir/kc_llm_version.tmp" "$$builddir/kc_llm_version.h"; \
	else \
		rm -f "$$builddir/kc_llm_version.tmp"; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-macos$(APPLE_BUILD_TAG),LD_LIBRARY_PATH="$(OSXCROSS_ROOT)/lib:$${LD_LIBRARY_PATH:-}" PATH="$(OSXCROSS_ROOT)/bin:$$PATH")
	@cp $(BUILD_DIR)/$(1)-macos$(APPLE_BUILD_TAG)/out/llm $(BIN_DIR)/$(1)/macos/llm
	@cp $(BUILD_DIR)/$(1)-macos$(APPLE_BUILD_TAG)/out/merged/libllm.a $(BIN_DIR)/$(1)/macos/libllm.a
	@cp $(BUILD_DIR)/$(1)-macos$(APPLE_BUILD_TAG)/out/libllm.dylib $(BIN_DIR)/$(1)/macos/libllm.dylib
	@echo "OK $(1)/macos"
endef

x86_64/macos:
	$(call macos_target,x86_64,$(OSXCROSS_X86_64_CC),$(OSXCROSS_X86_64_CXX),$(OSXCROSS_X86_64_AR),$(OSXCROSS_X86_64_RANLIB),x86_64)

aarch64/macos:
	$(call macos_target,aarch64,$(OSXCROSS_AARCH64_CC),$(OSXCROSS_AARCH64_CXX),$(OSXCROSS_AARCH64_AR),$(OSXCROSS_AARCH64_RANLIB),arm64)

## iOS

define ios_target
	@mkdir -p $(BIN_DIR)/$(1)/$(2)
	@if ! $(3) --version > /dev/null 2>&1; then \
		echo "Missing iOS cross-compiler wrapper: $(3)" >&2; \
		echo "Set OSXCROSS_ROOT to your osxcross target dir and ensure the wrappers are built." >&2; \
		exit 1; \
	fi
	@if [ -z "$(6)" ] || [ ! -d "$(6)" ]; then \
		echo "Missing iOS SDK sysroot: $(6)" >&2; \
		echo "Set $(5) to an installed Apple SDK directory." >&2; \
		exit 1; \
	fi
	@if [ -z "$(OSXCROSS_INSTALL_NAME_TOOL)" ] || [ ! -x "$(OSXCROSS_INSTALL_NAME_TOOL)" ]; then \
		echo "Missing osxcross install_name_tool under $(OSXCROSS_ROOT)/bin" >&2; \
		exit 1; \
	fi
	@if [ -z "$(8)" ] || [ ! -x "$(8)" ] || [ -z "$(9)" ] || [ ! -x "$(9)" ]; then \
		echo "Missing osxcross ar/ranlib for target $(1)/$(2) under $(OSXCROSS_ROOT)/bin" >&2; \
		exit 1; \
	fi
	@cache=$(BUILD_DIR)/$(1)-$(2)$(APPLE_BUILD_TAG)/CMakeCache.txt; \
	if [ -f "$$cache" ] && { ! grep -q '^CMAKE_C_COMPILER:.*=$(3)$$' "$$cache" || ! grep -q '^CMAKE_OSX_SYSROOT:.*=$(6)$$' "$$cache"; }; then \
		rm -f "$$cache" $(BUILD_DIR)/$(1)-$(2)$(APPLE_BUILD_TAG)/build.ninja && rm -rf $(BUILD_DIR)/$(1)-$(2)$(APPLE_BUILD_TAG)/CMakeFiles; \
	fi; \
	cached_sdk=$$(grep -E '^CMAKE_OSX_SYSROOT:PATH=' $(BUILD_DIR)/$(1)-$(2)$(APPLE_BUILD_TAG)/CMakeCache.txt 2>/dev/null | cut -d= -f2); \
	if [ ! -f $(BUILD_DIR)/$(1)-$(2)$(APPLE_BUILD_TAG)/build.ninja ] || [ -n "$$cached_sdk" ] && [ ! -d "$$cached_sdk" ]; then \
		builddir="$(BUILD_DIR)/$(1)-$(2)$(APPLE_BUILD_TAG)"; \
		mkdir -p "$$builddir"; \
		printf '#define KC_LLM_BUILD_VERSION %s\n#define KC_LLAMACPP_COMMIT "%s"\n' "$(BUILD_VERSION)" "$(LLAMACPP_COMMIT)" > "$$builddir/kc_llm_version.h"; \
		LD_LIBRARY_PATH="$(OSXCROSS_ROOT)/lib:$${LD_LIBRARY_PATH:-}" PATH="$(OSXCROSS_ROOT)/bin:$$PATH" cmake -S . -B "$$builddir" \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=iOS \
			-DCMAKE_SYSTEM_VERSION=$(IOS_DEPLOYMENT_TARGET) \
			-DCMAKE_OSX_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
			-DCMAKE_OSX_SYSROOT=$(6) \
			-DCMAKE_OSX_ARCHITECTURES=$(7) \
			-DCMAKE_INSTALL_NAME_TOOL=$(OSXCROSS_INSTALL_NAME_TOOL) \
			-DCMAKE_AR=$(8) \
			-DCMAKE_RANLIB=$(9) \
			-DKC_LLAMACPP_DIR=$(KC_LLAMACPP_DIR_FULL) \
			-DLLM_CUDA_ENABLED=OFF \
			-DCMAKE_C_COMPILER=$(3) \
			-DCMAKE_CXX_COMPILER=$(4) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$$builddir/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$$builddir/out \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$$builddir/out \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	@builddir="$(BUILD_DIR)/$(1)-$(2)$(APPLE_BUILD_TAG)"; \
	ver="#define KC_LLM_BUILD_VERSION $(BUILD_VERSION)\n#define KC_LLAMACPP_COMMIT \"$(LLAMACPP_COMMIT)\"\n"; \
	printf "$$ver" > "$$builddir/kc_llm_version.tmp"; \
	if ! diff -q "$$builddir/kc_llm_version.tmp" "$$builddir/kc_llm_version.h" > /dev/null 2>&1; then \
		mv "$$builddir/kc_llm_version.tmp" "$$builddir/kc_llm_version.h"; \
	else \
		rm -f "$$builddir/kc_llm_version.tmp"; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-$(2)$(APPLE_BUILD_TAG),LD_LIBRARY_PATH="$(OSXCROSS_ROOT)/lib:$${LD_LIBRARY_PATH:-}" PATH="$(OSXCROSS_ROOT)/bin:$$PATH")
	@if [ -f $(BUILD_DIR)/$(1)-$(2)$(APPLE_BUILD_TAG)/out/llm ]; then \
		cp $(BUILD_DIR)/$(1)-$(2)$(APPLE_BUILD_TAG)/out/llm $(BIN_DIR)/$(1)/$(2)/llm; \
	elif [ -f $(BUILD_DIR)/$(1)-$(2)$(APPLE_BUILD_TAG)/out/llm.app/llm ]; then \
		cp $(BUILD_DIR)/$(1)-$(2)$(APPLE_BUILD_TAG)/out/llm.app/llm $(BIN_DIR)/$(1)/$(2)/llm; \
	else \
		echo "Missing built iOS executable for $(1)/$(2)" >&2; \
		exit 1; \
	fi
	@cp $(BUILD_DIR)/$(1)-$(2)$(APPLE_BUILD_TAG)/out/merged/libllm.a $(BIN_DIR)/$(1)/$(2)/libllm.a
	@cp $(BUILD_DIR)/$(1)-$(2)$(APPLE_BUILD_TAG)/out/libllm.dylib $(BIN_DIR)/$(1)/$(2)/libllm.dylib
	@echo "OK $(1)/$(2)"
endef

aarch64/ios:
	$(call ios_target,aarch64,ios,$(OSXCROSS_IOS_AARCH64_CC),$(OSXCROSS_IOS_AARCH64_CXX),IPHONEOS_SDK,$(IPHONEOS_SDK),arm64,$(OSXCROSS_AARCH64_AR),$(OSXCROSS_AARCH64_RANLIB))

aarch64/iossim:
	$(call ios_target,aarch64,iossim,$(OSXCROSS_IOSSIM_AARCH64_CC),$(OSXCROSS_IOSSIM_AARCH64_CXX),IPHONESIMULATOR_SDK,$(IPHONESIMULATOR_SDK),arm64,$(OSXCROSS_AARCH64_AR),$(OSXCROSS_AARCH64_RANLIB))

x86_64/iossim:
	$(call ios_target,x86_64,iossim,$(OSXCROSS_IOSSIM_X86_64_CC),$(OSXCROSS_IOSSIM_X86_64_CXX),IPHONESIMULATOR_SDK,$(IPHONESIMULATOR_SDK),x86_64,$(OSXCROSS_X86_64_AR),$(OSXCROSS_X86_64_RANLIB))

## Android

define android_target
	@mkdir -p $(BIN_DIR)/$(1)/android
	@cache=$(BUILD_DIR)/$(1)-android/CMakeCache.txt && \
	build=$${cache%/CMakeCache.txt}/build.ninja; \
	if [ -f "$$cache" ] && { ! grep -q '^CMAKE_HOME_DIRECTORY:INTERNAL=$(CURDIR)$$' "$$cache" || [ ! -f "$$build" ] || ! grep -q '^CMAKE_TOOLCHAIN_FILE:.*=$(NDK_TOOLCHAIN)$$' "$$cache" || ! grep -q '^ANDROID_ABI:.*=$(2)$$' "$$cache"; }; then \
		rm -f "$$cache" $(BUILD_DIR)/$(1)-android/build.ninja && rm -rf $(BUILD_DIR)/$(1)-android/CMakeFiles; \
	fi
	@if [ ! -f $(BUILD_DIR)/$(1)-android/CMakeCache.txt ]; then \
		builddir="$(BUILD_DIR)/$(1)-android"; \
		mkdir -p "$$builddir"; \
		printf '#define KC_LLM_BUILD_VERSION %s\n#define KC_LLAMACPP_COMMIT "%s"\n' "$(BUILD_VERSION)" "$(LLAMACPP_COMMIT)" > "$$builddir/kc_llm_version.h"; \
		cmake -S . -B "$$builddir" \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_TOOLCHAIN_FILE=$(NDK_TOOLCHAIN) \
			-DKC_LLAMACPP_DIR=$(KC_LLAMACPP_DIR_FULL) \
			-DLLM_CUDA_ENABLED=OFF \
			-DANDROID_ABI=$(2) \
			-DANDROID_PLATFORM=android-21 \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$$builddir/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$$builddir/out \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$$builddir/out \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	@builddir="$(BUILD_DIR)/$(1)-android"; \
	ver="#define KC_LLM_BUILD_VERSION $(BUILD_VERSION)\n#define KC_LLAMACPP_COMMIT \"$(LLAMACPP_COMMIT)\"\n"; \
	printf "$$ver" > "$$builddir/kc_llm_version.tmp"; \
	if ! diff -q "$$builddir/kc_llm_version.tmp" "$$builddir/kc_llm_version.h" > /dev/null 2>&1; then \
		mv "$$builddir/kc_llm_version.tmp" "$$builddir/kc_llm_version.h"; \
	else \
		rm -f "$$builddir/kc_llm_version.tmp"; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-android)
	@cp $(BUILD_DIR)/$(1)-android/out/llm $(BIN_DIR)/$(1)/android/llm
	@cp $(BUILD_DIR)/$(1)-android/out/merged/libllm.a $(BIN_DIR)/$(1)/android/libllm.a
	@cp $(BUILD_DIR)/$(1)-android/out/libllm.so $(BIN_DIR)/$(1)/android/libllm.so
	@echo "OK $(1)/android"
endef

aarch64/android:
	$(call android_target,aarch64,arm64-v8a)

armv7/android:
	$(call android_target,armv7,armeabi-v7a)

## WebAssembly

define wasm_target
	@mkdir -p $(BIN_DIR)/wasm32/wasm
	@if ! command -v $(WASM_EMCMAKE) >/dev/null 2>&1 || ! command -v $(WASM_EMCC) >/dev/null 2>&1; then \
		echo "Missing Emscripten SDK toolchain: $(WASM_EMCMAKE)/$(WASM_EMCC)" >&2; \
		echo "Install the Emscripten SDK and add emcmake/emcc to PATH (e.g. 'source emsdk_env.sh')." >&2; \
		exit 1; \
	fi
	@cache=$(BUILD_DIR)/wasm32-wasm/CMakeCache.txt; \
	if [ -f "$$cache" ]; then \
		cached_dir=$$(grep -E '^CMAKE_HOME_DIRECTORY:INTERNAL=' "$$cache" 2>/dev/null | cut -d= -f2); \
		build=$${cache%/CMakeCache.txt}/build.ninja; \
		if [ "$$cached_dir" != "$(CURDIR)" ] || [ ! -f "$$build" ]; then \
			rm -f "$$cache" $(BUILD_DIR)/wasm32-wasm/build.ninja && rm -rf $(BUILD_DIR)/wasm32-wasm/CMakeFiles; \
		fi; \
	fi
	@if [ ! -f $(BUILD_DIR)/wasm32-wasm/CMakeCache.txt ]; then \
		builddir="$(BUILD_DIR)/wasm32-wasm"; \
		mkdir -p "$$builddir"; \
		printf '#define KC_LLM_BUILD_VERSION %s\n#define KC_LLAMACPP_COMMIT "%s"\n' "$(BUILD_VERSION)" "$(LLAMACPP_COMMIT)" > "$$builddir/kc_llm_version.h"; \
		$(WASM_EMCMAKE) cmake -S . -B "$$builddir" \
			-DCMAKE_BUILD_TYPE=Release \
			-DKC_LLAMACPP_DIR=$(KC_LLAMACPP_DIR_FULL) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$$builddir/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$$builddir/out \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$$builddir/out \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	@builddir="$(BUILD_DIR)/wasm32-wasm"; \
	ver="#define KC_LLM_BUILD_VERSION $(BUILD_VERSION)\n#define KC_LLAMACPP_COMMIT \"$(LLAMACPP_COMMIT)\"\n"; \
	printf "$$ver" > "$$builddir/kc_llm_version.tmp"; \
	if ! diff -q "$$builddir/kc_llm_version.tmp" "$$builddir/kc_llm_version.h" > /dev/null 2>&1; then \
		mv "$$builddir/kc_llm_version.tmp" "$$builddir/kc_llm_version.h"; \
	else \
		rm -f "$$builddir/kc_llm_version.tmp"; \
	fi
	$(call cmake_build,$(BUILD_DIR)/wasm32-wasm,)
	@cp $(BUILD_DIR)/wasm32-wasm/out/llm.wasm $(BIN_DIR)/wasm32/wasm/llm.wasm
	@echo "OK wasm32/wasm"
endef

wasm32/wasm:
	$(call wasm_target)

## Utility

test:
	@if [ -n "$(filter wine,$(MAKECMDGOALS))" ]; then \
		if ! command -v $(WINE) >/dev/null 2>&1; then \
			echo "Missing Wine runtime: $(WINE)" >&2; \
			exit 1; \
		fi; \
		if ! command -v $(WINE_X86_64_CC) >/dev/null 2>&1; then \
			echo "Missing Windows cross-compiler: $(WINE_X86_64_CC)" >&2; \
			exit 1; \
		fi; \
		if [ ! -f $(BIN_DIR)/x86_64/windows/libllm.dll ] || [ ! -f $(BIN_DIR)/x86_64/windows/libllm.dll.a ] || [ ! -f $(BIN_DIR)/x86_64/windows/llm.exe ]; then \
			echo "Missing Windows artifacts. Run 'make x86_64/windows' or 'make all' first." >&2; \
			exit 1; \
		fi; \
		if [ ! -f $(BUILD_DIR)/test-wine/CMakeCache.txt ]; then \
			builddir="$(BUILD_DIR)/test-wine"; \
			mkdir -p "$$builddir"; \
			cmake -S . -B "$$builddir" \
				-DCMAKE_BUILD_TYPE=Release \
				-DCMAKE_SYSTEM_NAME=Windows \
				-DCMAKE_C_COMPILER=$(WINE_X86_64_CC) \
				-DCMAKE_CXX_COMPILER=$(WINE_X86_64_CXX) \
				-DLLM_BUILD_TESTS=ON \
				-DKC_LLAMACPP_DIR=$(KC_LLAMACPP_DIR_FULL) \
				-DLLM_TEST_CLI=$(CURDIR)/$(BIN_DIR)/x86_64/windows/llm.exe \
				-DLLM_TEST_SHARED_LIBRARY=$(CURDIR)/$(BIN_DIR)/x86_64/windows/libllm.dll \
				-DLLM_TEST_IMPORT_LIBRARY=$(CURDIR)/$(BIN_DIR)/x86_64/windows/libllm.dll.a \
				$(if $(KC_LLM_TEST_MODEL),-DLLM_TEST_MODEL=$(KC_LLM_TEST_MODEL),) \
				$(if $(KC_LLM_TEST_LORA),-DLLM_TEST_LORA=$(KC_LLM_TEST_LORA),) \
				-DCMAKE_CROSSCOMPILING_EMULATOR=$(WINE) \
				-G Ninja -Wno-dev > /dev/null; \
		fi; \
		cmake --build $(BUILD_DIR)/test-wine --target llm_contract_test; \
		EGL_LOG_LEVEL=fatal WINEDEBUG=-all $(WINE) $(BUILD_DIR)/test-wine/llm_contract_test.exe all; \
	elif [ -n "$(filter wasm wasm32/wasm,$(MAKECMDGOALS))" ]; then \
		if ! command -v $(WASM_EMCMAKE) >/dev/null 2>&1 || ! command -v $(WASM_EMCC) >/dev/null 2>&1; then \
			echo "Missing Emscripten SDK toolchain: $(WASM_EMCMAKE)/$(WASM_EMCC)" >&2; \
			echo "Install the Emscripten SDK and add emcmake/emcc to PATH (e.g. 'source emsdk_env.sh')." >&2; \
			exit 1; \
		fi; \
		if [ ! -f $(BIN_DIR)/wasm32/wasm/llm.wasm ]; then \
			echo "Missing $(BIN_DIR)/wasm32/wasm/llm.wasm. Run 'make wasm32/wasm' first." >&2; \
			exit 1; \
		fi; \
		if [ ! -f $(BUILD_DIR)/test-wasm/CMakeCache.txt ]; then \
			mkdir -p $(BUILD_DIR)/test-wasm; \
			printf '#define KC_LLM_BUILD_VERSION %s\n#define KC_LLAMACPP_COMMIT "%s"\n' "$(BUILD_VERSION)" "$(LLAMACPP_COMMIT)" > $(BUILD_DIR)/test-wasm/kc_llm_version.h; \
			$(WASM_EMCMAKE) cmake -S . -B $(BUILD_DIR)/test-wasm \
				-DCMAKE_BUILD_TYPE=Release \
				-DLLM_BUILD_TESTS=ON \
				-DKC_LLAMACPP_DIR=$(KC_LLAMACPP_DIR_FULL) \
				-DLLM_TEST_MODEL=$(if $(filter /%,$(KC_LLM_TEST_MODEL)),$(patsubst $(CURDIR)/%,%,$(KC_LLM_TEST_MODEL)),$(KC_LLM_TEST_MODEL)) \
				-DLLM_TEST_LORA=$(if $(filter /%,$(KC_LLM_TEST_LORA)),$(patsubst $(CURDIR)/%,%,$(KC_LLM_TEST_LORA)),$(KC_LLM_TEST_LORA)) \
				-G Ninja -Wno-dev > /dev/null; \
		fi; \
		cmake --build $(BUILD_DIR)/test-wasm --target llm_contract_test || exit 1; \
		if [ -n "$(EMSDK_NODE)" ]; then node="$(EMSDK_NODE)"; else node="$(WASM_NODE)"; fi; \
		if ! command -v "$$node" >/dev/null 2>&1; then \
			echo "Missing Emscripten test runtime: $$node" >&2; \
			exit 1; \
		fi; \
		"$$node" $(BUILD_DIR)/test-wasm/llm_contract_test.js all; \
	else \
		if [ "$(NATIVE_ARCH)" = "unsupported" ] || [ "$(NATIVE_PLATFORM)" = "unsupported" ]; then \
			echo "Unsupported native test target $(HOST_ARCH)/$(HOST_SYSTEM)" >&2; \
			exit 1; \
		fi; \
		if [ ! -f $(BIN_DIR)/$(NATIVE_TARGET)/$(NATIVE_SHARED_NAME) ] || [ ! -f $(BIN_DIR)/$(NATIVE_TARGET)/llm$(NATIVE_EXE_EXT) ]; then \
			echo "Missing native artifacts. Run 'make' first." >&2; \
			exit 1; \
		fi; \
		if [ ! -f $(BUILD_DIR)/test/CMakeCache.txt ]; then \
			builddir="$(BUILD_DIR)/test"; \
			mkdir -p "$$builddir"; \
			cmake -S . -B "$$builddir" \
				-DCMAKE_BUILD_TYPE=Release \
				-DLLM_BUILD_TESTS=ON \
				-DKC_LLAMACPP_DIR=$(KC_LLAMACPP_DIR_FULL) \
				-DLLM_TEST_CLI=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/llm$(NATIVE_EXE_EXT) \
				-DLLM_TEST_SHARED_LIBRARY=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/$(NATIVE_SHARED_NAME) \
				$(NATIVE_IMPORT_LIBRARY) \
				$(if $(KC_LLM_TEST_MODEL),-DLLM_TEST_MODEL=$(KC_LLM_TEST_MODEL),) \
				$(if $(KC_LLM_TEST_LORA),-DLLM_TEST_LORA=$(KC_LLM_TEST_LORA),) \
				-G Ninja -Wno-dev > /dev/null; \
		fi; \
		cmake --build $(BUILD_DIR)/test --target llm_contract_test; \
		$(BUILD_DIR)/test/llm_contract_test all; \
	fi

wine:
	@if [ -z "$(filter test,$(MAKECMDGOALS))" ]; then \
		echo "Use 'make test wine' to run tests through Wine." >&2; \
		exit 1; \
	fi
	@:

wasm:
	@if [ -z "$(filter test,$(MAKECMDGOALS))" ]; then \
		echo "Use 'make test wasm' to run tests through Emscripten and Node.js." >&2; \
		exit 1; \
	fi
	@:

clean:
	@rm -rf $(BUILD_DIR)
	@echo "OK clean"
