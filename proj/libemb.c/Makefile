## Makefile
## Summary: Cross-compilation builder for emb.
##
## Author:  KaisarCode
## Website: https://kaisarcode.com
## License: https://www.gnu.org/licenses/lgpl-3.0.html

ANDROID_HOME  ?= $(HOME)/.local/share/android-sdk
NDK_VERSION   ?= 27.2.12479018
NDK_DIR       := $(ANDROID_HOME)/ndk/$(NDK_VERSION)
NDK_TOOLCHAIN := $(NDK_DIR)/build/cmake/android.toolchain.cmake
WINE ?= wine
WINE_X86_64_CC ?= x86_64-w64-mingw32-gcc
WINE_X86_64_CXX ?= x86_64-w64-mingw32-g++
WASM_EMCMAKE ?= emcmake
WASM_EMCC    ?= emcc
WASM_NODE    ?= node
MIPSEL_CC ?= mipsel-linux-gnu-gcc-13
MIPSEL_CXX ?= mipsel-linux-gnu-g++-13
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
OSXCROSS_AARCH64_CC := $(OSXCROSS_ROOT)/bin/oa64-clang
OSXCROSS_IOS_AARCH64_CC := $(OSXCROSS_ROOT)/bin/ios64-clang
OSXCROSS_IOSSIM_AARCH64_CC := $(OSXCROSS_ROOT)/bin/iossim64-clang
OSXCROSS_IOSSIM_X86_64_CC := $(OSXCROSS_ROOT)/bin/iossimx64-clang
OSXCROSS_X86_64_AR := $(shell ls "$(OSXCROSS_ROOT)"/bin/x86_64-apple-darwin*-ar 2>/dev/null | sort -V | tail -n 1)
OSXCROSS_X86_64_RANLIB := $(shell ls "$(OSXCROSS_ROOT)"/bin/x86_64-apple-darwin*-ranlib 2>/dev/null | sort -V | tail -n 1)
OSXCROSS_AARCH64_AR := $(shell ls "$(OSXCROSS_ROOT)"/bin/aarch64-apple-darwin*-ar 2>/dev/null | sort -V | tail -n 1)
OSXCROSS_AARCH64_RANLIB := $(shell ls "$(OSXCROSS_ROOT)"/bin/aarch64-apple-darwin*-ranlib 2>/dev/null | sort -V | tail -n 1)
OSXCROSS_IOS_AARCH64_AR := $(OSXCROSS_AARCH64_AR)
OSXCROSS_IOS_AARCH64_RANLIB := $(OSXCROSS_AARCH64_RANLIB)
OSXCROSS_IOSSIM_AARCH64_AR := $(OSXCROSS_AARCH64_AR)
OSXCROSS_IOSSIM_AARCH64_RANLIB := $(OSXCROSS_AARCH64_RANLIB)
OSXCROSS_IOSSIM_X86_64_AR := $(OSXCROSS_X86_64_AR)
OSXCROSS_IOSSIM_X86_64_RANLIB := $(OSXCROSS_X86_64_RANLIB)

BUILD_DIR := .build
BIN_DIR   := bin
BUILD_VERSION ?= $(shell date +%s)

define cmake_build
	@prelog=$$(mktemp); \
	if ! $(3) cmake --build $(1) -- -n > "$$prelog" 2>&1; then \
		cat "$$prelog"; \
		rm -f "$$prelog"; \
		exit 1; \
	fi; \
	if grep -q "ninja: no work to do." "$$prelog"; then \
		rm -f "$$prelog"; \
		out=$$(mktemp); \
		$(3) cmake --build $(1) 2>"$$out"; \
		r=$$?; \
		if [ -s "$$out" ]; then grep -v 'skipping incompatible' < "$$out"; fi; \
		rm -f "$$out"; \
		exit $$r; \
	fi; \
	rm -f "$$prelog"; \
	out=$$(mktemp); \
	$(3) cmake --build $(1) 2>"$$out"; \
	r=$$?; \
	if [ -s "$$out" ]; then grep -v 'skipping incompatible' < "$$out"; fi; \
	rm -f "$$out"; \
	if [ $$r -ne 0 ]; then \
		exit 1; \
	fi; \
	if [ -n "$(2)" ]; then \
		ver=$$(date +%s); \
		$(if $(2),$(2);,) \
		log=$$(mktemp); \
		if ! $(3) cmake --build $(1) > "$$log" 2>&1; then \
			cat "$$log"; \
			rm -f "$$log"; \
			exit 1; \
		fi; \
		rm -f "$$log"; \
	fi; \
	:
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

ifeq ($(HOST_SYSTEM),Darwin)
NATIVE_PLATFORM := macos
endif

ifneq ($(filter MINGW% MSYS% CYGWIN%,$(HOST_SYSTEM)),)
NATIVE_PLATFORM := windows
endif

NATIVE_TARGET := $(NATIVE_PLATFORM)/$(NATIVE_ARCH)
NATIVE_SHARED_NAME := libemb.so
NATIVE_CLI_NAME := emb
NATIVE_IMPORT_LIBRARY :=

ifeq ($(NATIVE_PLATFORM),windows)
NATIVE_SHARED_NAME := libemb.dll
NATIVE_CLI_NAME := emb.exe
NATIVE_IMPORT_LIBRARY := -DEMB_TEST_IMPORT_LIBRARY=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/libemb.dll.a
endif

ifeq ($(NATIVE_PLATFORM),macos)
NATIVE_SHARED_NAME := libemb.dylib
endif

.DEFAULT_GOAL := $(NATIVE_TARGET)

.PHONY: all test wine wasm clean \
	linux/x86_64 windows/x86_64 macos/x86_64 \
	iossim/x86_64 \
	linux/i686 windows/i686 \
	linux/aarch64 android/aarch64 macos/aarch64 ios/aarch64 iossim/aarch64 \
	linux/armv7 android/armv7 \
	linux/armv7hf \
	linux/riscv64 \
	linux/powerpc64le \
	linux/mips linux/mipsel linux/mips64el \
	linux/s390x \
	linux/loongarch64 \
	wasm/wasm32

all: \
	linux/x86_64 windows/x86_64 macos/x86_64 \
	iossim/x86_64 \
	linux/i686 windows/i686 \
	linux/aarch64 android/aarch64 macos/aarch64 ios/aarch64 iossim/aarch64 \
	linux/armv7 android/armv7 \
	linux/armv7hf \
	linux/riscv64 \
	linux/powerpc64le \
	linux/mips linux/mipsel linux/mips64el \
	linux/s390x \
	linux/loongarch64 \
	wasm/wasm32

## Linux
## $(1)=target  $(2)=processor  $(3)=CC  $(4)=CXX  $(5)=build environment

define linux_target
	@mkdir -p $(BIN_DIR)/linux/$(1)
	@cache=$(BUILD_DIR)/$(subst /,-,$(1))-linux/CMakeCache.txt && \
	if [ -f "$cache" ] && { ! grep -Fq "CMAKE_C_COMPILER:FILEPATH=$(command -v $(3) 2>/dev/null || printf '%s' $(3))" "$cache" || ! grep -Fq "CMAKE_CXX_COMPILER:FILEPATH=$(command -v $(4) 2>/dev/null || printf '%s' $(4))" "$cache"; }; then \
		rm -f "$cache" $(BUILD_DIR)/$(subst /,-,$(1))-linux/build.ninja && rm -rf $(BUILD_DIR)/$(subst /,-,$(1))-linux/CMakeFiles; \
	fi
	@if [ ! -f $(BUILD_DIR)/$(subst /,-,$(1))-linux/CMakeCache.txt ] || ! grep -Fq '$(CURDIR)/$(BIN_DIR)/linux/$(1)' $(BUILD_DIR)/$(subst /,-,$(1))-linux/CMakeCache.txt; then \
		cmake -S . -B $(BUILD_DIR)/$(subst /,-,$(1))-linux \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Linux \
			-DCMAKE_SYSTEM_PROCESSOR=$(2) \
			-DEMB_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_C_COMPILER=$(3) \
			-DCMAKE_CXX_COMPILER=$(4) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(subst /,-,$(1))-linux/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/linux/$(1) \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/linux/$(1) \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(subst /,-,$(1))-linux,cmake -S . -B $(BUILD_DIR)/$(subst /,-,$(1))-linux -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=$(2) -DEMB_BUILD_VERSION=$(BUILD_VERSION) -DCMAKE_C_COMPILER=$(3) -DCMAKE_CXX_COMPILER=$(4) -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(subst /,-,$(1))-linux/out -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/linux/$(1) -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/linux/$(1) -G Ninja -Wno-dev > /dev/null,$(5))
	@echo "OK linux/$(1)"
endef

linux/x86_64:
	$(call linux_target,x86_64,x86_64,gcc,g++)

linux/i686:
	$(call linux_target,i686,i686,i686-linux-gnu-gcc,i686-linux-gnu-g++)

linux/aarch64:
	$(call linux_target,aarch64,aarch64,aarch64-linux-gnu-gcc,aarch64-linux-gnu-g++)

linux/armv7hf:
	$(call linux_target,armv7hf,armv7,arm-linux-gnueabihf-gcc,arm-linux-gnueabihf-g++)

linux/armv7:
	$(call linux_target,armv7,armv7,arm-linux-gnueabi-gcc,arm-linux-gnueabi-g++)

linux/riscv64:
	$(call linux_target,riscv64,riscv64,riscv64-linux-gnu-gcc,riscv64-linux-gnu-g++)

linux/powerpc64le:
	$(call linux_target,powerpc64le,powerpc64le,powerpc64le-linux-gnu-gcc,powerpc64le-linux-gnu-g++)

linux/mips:
	$(call linux_target,mips,mips,mips-linux-gnu-gcc,mips-linux-gnu-g++)

linux/mipsel:
	@command -v $(MIPSEL_CC) >/dev/null 2>&1 || { echo "Missing MIPS little-endian C compiler: $(MIPSEL_CC)" >&2; exit 1; }
	@command -v $(MIPSEL_CXX) >/dev/null 2>&1 || { echo "Missing MIPS little-endian C++ compiler: $(MIPSEL_CXX)" >&2; exit 1; }
	$(call linux_target,mipsel,mipsel,$(MIPSEL_CC),$(MIPSEL_CXX),CMAKE_BUILD_PARALLEL_LEVEL=1)

linux/mips64el:
	$(call linux_target,mips64el,mips64el,mips64el-linux-gnuabi64-gcc,mips64el-linux-gnuabi64-g++)

linux/s390x:
	$(call linux_target,s390x,s390x,s390x-linux-gnu-gcc,s390x-linux-gnu-g++)

linux/loongarch64:
	$(call linux_target,loongarch64,loongarch64,loongarch64-linux-gnu-gcc,loongarch64-linux-gnu-g++)

## Windows
## $(1)=target  $(2)=processor  $(3)=CC  $(4)=CXX

define windows_target
	@mkdir -p $(BIN_DIR)/windows/$(1)
	@cache=$(BUILD_DIR)/$(1)-windows/CMakeCache.txt && \
	if [ -f "$cache" ] && { ! grep -Fq "CMAKE_C_COMPILER:FILEPATH=$(command -v $(3) 2>/dev/null || printf '%s' $(3))" "$cache" || ! grep -Fq "CMAKE_CXX_COMPILER:FILEPATH=$(command -v $(4) 2>/dev/null || printf '%s' $(4))" "$cache"; }; then \
		rm -f "$cache" $(BUILD_DIR)/$(1)-windows/build.ninja && rm -rf $(BUILD_DIR)/$(1)-windows/CMakeFiles; \
	fi
	@if [ ! -f $(BUILD_DIR)/$(1)-windows/CMakeCache.txt ] || ! grep -Fq '$(CURDIR)/$(BIN_DIR)/windows/$(1)' $(BUILD_DIR)/$(1)-windows/CMakeCache.txt; then \
		cmake -S . -B $(BUILD_DIR)/$(1)-windows \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Windows \
			-DCMAKE_SYSTEM_PROCESSOR=$(2) \
			-DEMB_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_C_COMPILER=$(3) \
			-DCMAKE_CXX_COMPILER=$(4) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-windows/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/windows/$(1) \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/windows/$(1) \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-windows,cmake -S . -B $(BUILD_DIR)/$(1)-windows -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=$(2) -DEMB_BUILD_VERSION=$(BUILD_VERSION) -DCMAKE_C_COMPILER=$(3) -DCMAKE_CXX_COMPILER=$(4) -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-windows/out -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/windows/$(1) -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/windows/$(1) -G Ninja -Wno-dev > /dev/null,CMAKE_BUILD_PARALLEL_LEVEL=1)
	@cp $(BUILD_DIR)/$(1)-windows/out/libemb.dll $(BIN_DIR)/windows/$(1)/libemb.dll
	@echo "OK windows/$(1)"
endef

windows/x86_64:
	$(call windows_target,x86_64,x86_64,x86_64-w64-mingw32-gcc,x86_64-w64-mingw32-g++)

windows/i686:
	$(call windows_target,i686,i686,i686-w64-mingw32-gcc,i686-w64-mingw32-g++)

## macOS
## $(1)=target  $(2)=processor  $(3)=CC  $(4)=CXX  $(5)=AR  $(6)=RANLIB

define macos_target
	@mkdir -p $(BIN_DIR)/macos/$(1)
	@if ! $(3) --version > /dev/null 2>&1; then \
		echo "Missing macOS cross-compiler wrapper: $(3)" >&2; \
		echo "Set OSXCROSS_ROOT to your osxcross target dir and ensure the wrappers are built." >&2; \
		exit 1; \
	fi
	@cache=$(BUILD_DIR)/$(1)-macos/CMakeCache.txt && \
	if [ -f "$$cache" ] && ! grep -q '^CMAKE_SYSTEM_PROCESSOR:.*=$(2)$$' "$$cache"; then \
		rm -f "$$cache" $(BUILD_DIR)/$(1)-macos/build.ninja && rm -rf $(BUILD_DIR)/$(1)-macos/CMakeFiles; \
	fi
	@if [ ! -f $(BUILD_DIR)/$(1)-macos/build.ninja ] || ! grep -Fq '$(CURDIR)/$(BIN_DIR)/macos/$(1)' $(BUILD_DIR)/$(1)-macos/CMakeCache.txt; then \
		LD_LIBRARY_PATH="$(OSXCROSS_ROOT)/lib:$${LD_LIBRARY_PATH:-}" PATH="$(OSXCROSS_ROOT)/bin:$$PATH" cmake -S . -B $(BUILD_DIR)/$(1)-macos \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Darwin \
			-DCMAKE_SYSTEM_PROCESSOR=$(2) \
			-DCMAKE_OSX_DEPLOYMENT_TARGET=$(MACOSX_DEPLOYMENT_TARGET) \
			-DEMB_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_C_COMPILER=$(3) \
			-DCMAKE_CXX_COMPILER=$(4) \
			-DCMAKE_AR=$(5) \
			-DCMAKE_RANLIB=$(6) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-macos/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/macos/$(1) \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/macos/$(1) \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-macos,,LD_LIBRARY_PATH="$(OSXCROSS_ROOT)/lib:$${LD_LIBRARY_PATH:-}" PATH="$(OSXCROSS_ROOT)/bin:$$PATH")
	@echo "OK macos/$(1)"
endef

macos/x86_64:
	$(call macos_target,x86_64,x86_64,$(OSXCROSS_X86_64_CC),$(OSXCROSS_X86_64_CC)++,$(OSXCROSS_X86_64_AR),$(OSXCROSS_X86_64_RANLIB))

macos/aarch64:
	$(call macos_target,aarch64,arm64,$(OSXCROSS_AARCH64_CC),$(OSXCROSS_AARCH64_CC)++,$(OSXCROSS_AARCH64_AR),$(OSXCROSS_AARCH64_RANLIB))

## iOS
## $(1)=target  $(2)=platform  $(3)=CC  $(4)=SDK_VAR  $(5)=SDK  $(6)=ARCH  $(7)=AR  $(8)=RANLIB

define ios_target
	@mkdir -p $(BIN_DIR)/$(2)/$(1)
	@if ! $(3) --version > /dev/null 2>&1; then \
		echo "Missing iOS cross-compiler wrapper: $(3)" >&2; \
		echo "Set OSXCROSS_ROOT to your osxcross target dir and ensure the wrappers are built." >&2; \
		exit 1; \
	fi
	@if [ -z "$(5)" ] || [ ! -d "$(5)" ]; then \
		echo "Missing iOS SDK sysroot: $(5)" >&2; \
		echo "Set $(4) to an installed Apple SDK directory." >&2; \
		exit 1; \
	fi
	@cache=$(BUILD_DIR)/$(1)-$(2)/CMakeCache.txt && \
	if [ -f "$$cache" ] && { ! grep -q '^CMAKE_C_COMPILER:.*=$(3)$$' "$$cache" || ! grep -q '^CMAKE_OSX_SYSROOT:.*=$(5)$$' "$$cache"; }; then \
		rm -f "$$cache" $(BUILD_DIR)/$(1)-$(2)/build.ninja && rm -rf $(BUILD_DIR)/$(1)-$(2)/CMakeFiles; \
	fi
	@if [ ! -f $(BUILD_DIR)/$(1)-$(2)/build.ninja ] || ! grep -Fq '$(CURDIR)/$(BIN_DIR)/$(2)/$(1)' $(BUILD_DIR)/$(1)-$(2)/CMakeCache.txt; then \
		LD_LIBRARY_PATH="$(OSXCROSS_ROOT)/lib:$${LD_LIBRARY_PATH:-}" PATH="$(OSXCROSS_ROOT)/bin:$$PATH" cmake -S . -B $(BUILD_DIR)/$(1)-$(2) \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=iOS \
			-DCMAKE_SYSTEM_PROCESSOR=$(6) \
			-DCMAKE_SYSTEM_VERSION=$(IOS_DEPLOYMENT_TARGET) \
			-DCMAKE_OSX_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
			-DCMAKE_OSX_SYSROOT=$(5) \
			-DCMAKE_OSX_ARCHITECTURES=$(6) \
			-DEMB_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_C_COMPILER=$(3) \
			-DCMAKE_CXX_COMPILER=$(3)++ \
			-DCMAKE_AR=$(7) \
			-DCMAKE_RANLIB=$(8) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-$(2)/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(2)/$(1) \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(2)/$(1) \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-$(2),,LD_LIBRARY_PATH="$(OSXCROSS_ROOT)/lib:$${LD_LIBRARY_PATH:-}" PATH="$(OSXCROSS_ROOT)/bin:$$PATH")
	@echo "OK $(2)/$(1)"
endef

ios/aarch64:
	$(call ios_target,aarch64,ios,$(OSXCROSS_IOS_AARCH64_CC),IPHONEOS_SDK,$(IPHONEOS_SDK),arm64,$(OSXCROSS_IOS_AARCH64_AR),$(OSXCROSS_IOS_AARCH64_RANLIB))

iossim/aarch64:
	$(call ios_target,aarch64,iossim,$(OSXCROSS_IOSSIM_AARCH64_CC),IPHONESIMULATOR_SDK,$(IPHONESIMULATOR_SDK),arm64,$(OSXCROSS_IOSSIM_AARCH64_AR),$(OSXCROSS_IOSSIM_AARCH64_RANLIB))

iossim/x86_64:
	$(call ios_target,x86_64,iossim,$(OSXCROSS_IOSSIM_X86_64_CC),IPHONESIMULATOR_SDK,$(IPHONESIMULATOR_SDK),x86_64,$(OSXCROSS_IOSSIM_X86_64_AR),$(OSXCROSS_IOSSIM_X86_64_RANLIB))

## Android

define android_target
	@mkdir -p $(BIN_DIR)/android/$(1)
	@cache=$(BUILD_DIR)/$(1)-android/CMakeCache.txt && \
	if [ -f "$$cache" ] && { ! grep -q '^CMAKE_TOOLCHAIN_FILE:.*=$(NDK_TOOLCHAIN)$$' "$$cache" || ! grep -q '^ANDROID_ABI:.*=$(2)$$' "$$cache"; }; then \
		rm -f "$$cache" $(BUILD_DIR)/$(1)-android/build.ninja && rm -rf $(BUILD_DIR)/$(1)-android/CMakeFiles; \
	fi
	@if [ ! -f $(BUILD_DIR)/$(1)-android/CMakeCache.txt ] || ! grep -Fq '$(CURDIR)/$(BIN_DIR)/android/$(1)' $(BUILD_DIR)/$(1)-android/CMakeCache.txt; then \
		cmake -S . -B $(BUILD_DIR)/$(1)-android \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_TOOLCHAIN_FILE=$(NDK_TOOLCHAIN) \
			-DEMB_BUILD_VERSION=$(BUILD_VERSION) \
			-DANDROID_ABI=$(2) \
			-DANDROID_PLATFORM=android-21 \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-android/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/android/$(1) \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/android/$(1) \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-android,,)
	@echo "OK android/$(1)"
endef

android/aarch64:
	$(call android_target,aarch64,arm64-v8a)

android/armv7:
	$(call android_target,armv7,armeabi-v7a)

## WebAssembly

define wasm_target
	@mkdir -p $(BIN_DIR)/wasm/wasm32
	@if ! command -v $(WASM_EMCMAKE) >/dev/null 2>&1 || ! command -v $(WASM_EMCC) >/dev/null 2>&1; then \
		echo "Missing Emscripten SDK toolchain: $(WASM_EMCMAKE)/$(WASM_EMCC)" >&2; \
		echo "Install the Emscripten SDK and add emcmake/emcc to PATH (e.g. 'source emsdk_env.sh')." >&2; \
		exit 1; \
	fi
	@if [ ! -f $(BUILD_DIR)/wasm32-wasm/CMakeCache.txt ]; then \
		$(WASM_EMCMAKE) cmake -S . -B $(BUILD_DIR)/wasm32-wasm \
			-DCMAKE_BUILD_TYPE=Release \
			-DEMB_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/wasm32-wasm/out \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/wasm32-wasm,wasm_log=$$(mktemp); if ! $(WASM_EMCMAKE) cmake -S . -B $(BUILD_DIR)/wasm32-wasm -DCMAKE_BUILD_TYPE=Release -DEMB_BUILD_VERSION=$$ver -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/wasm32-wasm/out -G Ninja -Wno-dev > "$$wasm_log" 2>&1; then cat "$$wasm_log"; rm -f "$$wasm_log"; exit 1; fi; rm -f "$$wasm_log")
	@cp $(BUILD_DIR)/wasm32-wasm/out/libemb.wasm $(BIN_DIR)/wasm/wasm32/libemb.wasm
	@echo "OK wasm/wasm32"
endef

wasm/wasm32:
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
		if ! command -v $(WINE_X86_64_CXX) >/dev/null 2>&1; then \
			echo "Missing Windows cross-compiler: $(WINE_X86_64_CXX)" >&2; \
			exit 1; \
		fi; \
		if [ ! -f $(BIN_DIR)/windows/x86_64/libemb.dll ] || [ ! -f $(BIN_DIR)/windows/x86_64/libemb.dll.a ] || [ ! -f $(BIN_DIR)/windows/x86_64/emb.exe ]; then \
			echo "Missing Windows artifacts. Run 'make windows/x86_64' or 'make all' first." >&2; \
			exit 1; \
		fi; \
		if [ ! -f $(BUILD_DIR)/test-wine/CMakeCache.txt ] || ! grep -Fq '$(CURDIR)/$(BIN_DIR)/windows/x86_64' $(BUILD_DIR)/test-wine/CMakeCache.txt; then \
			cmake -S . -B $(BUILD_DIR)/test-wine \
				-DCMAKE_BUILD_TYPE=Release \
				-DCMAKE_SYSTEM_NAME=Windows \
				-DCMAKE_SYSTEM_PROCESSOR=x86_64 \
				-DCMAKE_C_COMPILER=$(WINE_X86_64_CC) \
				-DCMAKE_CXX_COMPILER=$(WINE_X86_64_CXX) \
				-DEMB_BUILD_TESTS=ON \
				-DEMB_BUILD_VERSION=$(BUILD_VERSION) \
				-DEMB_TEST_SHARED_LIBRARY=$(CURDIR)/$(BIN_DIR)/windows/x86_64/libemb.dll \
				-DEMB_TEST_IMPORT_LIBRARY=$(CURDIR)/$(BIN_DIR)/windows/x86_64/libemb.dll.a \
			-DEMB_CLI_PATH=$(CURDIR)/$(BIN_DIR)/windows/x86_64/emb.exe \
				-DCMAKE_CROSSCOMPILING_EMULATOR=$(WINE) \
				-G Ninja -Wno-dev > /dev/null; \
		fi; \
		cmake --build $(BUILD_DIR)/test-wine --target emb_contract_test; \
		for dll in \
			"$$($(WINE_X86_64_CC) -print-file-name=libgcc_s_seh-1.dll)" \
			"$$($(WINE_X86_64_CXX) -print-file-name=libstdc++-6.dll)" \
			"$$($(WINE_X86_64_CXX) -print-file-name=libwinpthread-1.dll)"; do \
			if [ -f "$$dll" ]; then \
				cp "$$dll" $(BUILD_DIR)/test-wine/; \
				cp "$$dll" $(BIN_DIR)/windows/x86_64/; \
			fi; \
		done; \
		EGL_LOG_LEVEL=fatal WINEDEBUG=-all $(WINE) $(BUILD_DIR)/test-wine/emb_contract_test.exe all; \
	elif [ -n "$(filter wasm,$(MAKECMDGOALS))" ]; then \
		if ! command -v $(WASM_EMCMAKE) >/dev/null 2>&1 || ! command -v $(WASM_EMCC) >/dev/null 2>&1; then \
			echo "Missing Emscripten SDK toolchain: $(WASM_EMCMAKE)/$(WASM_EMCC)" >&2; \
			echo "Install the Emscripten SDK and add emcmake/emcc to PATH (e.g. 'source emsdk_env.sh')." >&2; \
			exit 1; \
		fi; \
		if [ ! -f $(BIN_DIR)/wasm/wasm32/libemb.wasm ]; then \
			echo "Missing $(BIN_DIR)/wasm/wasm32/libemb.wasm. Run 'make wasm/wasm32' first." >&2; \
			exit 1; \
		fi; \
		if [ ! -f $(BUILD_DIR)/test-wasm/CMakeCache.txt ]; then \
			$(WASM_EMCMAKE) cmake -S . -B $(BUILD_DIR)/test-wasm \
				-DCMAKE_BUILD_TYPE=Release \
				-DEMB_BUILD_TESTS=ON \
				-DEMB_BUILD_VERSION=$(BUILD_VERSION) \
				-G Ninja -Wno-dev > /dev/null; \
		fi; \
		cmake --build $(BUILD_DIR)/test-wasm --target emb_contract_test || exit 1; \
		if [ -n "$(EMSDK_NODE)" ]; then node="$(EMSDK_NODE)"; else node="$(WASM_NODE)"; fi; \
		if ! command -v "$$node" >/dev/null 2>&1; then \
			echo "Missing Emscripten test runtime: $$node" >&2; \
			exit 1; \
		fi; \
		"$$node" $(BUILD_DIR)/test-wasm/emb_contract_test.js all; \
	else \
		if [ "$(NATIVE_ARCH)" = "unsupported" ] || [ "$(NATIVE_PLATFORM)" = "unsupported" ]; then \
			echo "Unsupported native test target $(HOST_ARCH)/$(HOST_SYSTEM)" >&2; \
			exit 1; \
		fi; \
		if [ ! -f $(BIN_DIR)/$(NATIVE_TARGET)/$(NATIVE_SHARED_NAME) ] || [ ! -f $(BIN_DIR)/$(NATIVE_TARGET)/$(NATIVE_CLI_NAME) ]; then \
			echo "Missing native artifacts. Run 'make' first." >&2; \
			exit 1; \
		fi; \
		if [ ! -f $(BUILD_DIR)/test/CMakeCache.txt ] || ! grep -Fq '$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)' $(BUILD_DIR)/test/CMakeCache.txt; then \
			cmake -S . -B $(BUILD_DIR)/test \
				-DCMAKE_BUILD_TYPE=Release \
				-DCMAKE_SYSTEM_PROCESSOR=$(NATIVE_ARCH) \
				-DEMB_BUILD_TESTS=ON \
				-DEMB_BUILD_VERSION=$(BUILD_VERSION) \
				-DEMB_TEST_SHARED_LIBRARY=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/$(NATIVE_SHARED_NAME) \
				-DEMB_CLI_PATH=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/$(NATIVE_CLI_NAME) \
				$(NATIVE_IMPORT_LIBRARY) \
				-G Ninja -Wno-dev > /dev/null; \
		fi; \
		cmake --build $(BUILD_DIR)/test --target emb_contract_test; \
		$(BUILD_DIR)/test/emb_contract_test all; \
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
