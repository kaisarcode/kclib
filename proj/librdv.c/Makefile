## Makefile
## Summary: Cross-compilation builder for rdv.
##
## Author:  KaisarCode
## Website: https://kaisarcode.com
## License: https://www.gnu.org/licenses/lgpl-3.0.html

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
OSXCROSS_X86_64_CC := $(OSXCROSS_ROOT)/bin/o64-clang
OSXCROSS_AARCH64_CC := $(OSXCROSS_ROOT)/bin/oa64-clang
OSXCROSS_IOS_AARCH64_CC := $(OSXCROSS_ROOT)/bin/ios64-clang
OSXCROSS_IOSSIM_AARCH64_CC := $(OSXCROSS_ROOT)/bin/iossim64-clang
OSXCROSS_IOSSIM_X86_64_CC := $(OSXCROSS_ROOT)/bin/iossimx64-clang
WINE ?= wine
WINE_X86_64_CC ?= x86_64-w64-mingw32-gcc
WASM_EMCMAKE ?= emcmake
WASM_NODE ?= node

BUILD_DIR := .build
BIN_DIR := bin
BUILD_VERSION ?= $(shell date +%s)
CMAKE ?= cmake

define cmake_build
	@prelog=$$(mktemp); \
	if ! $(CMAKE) --build $(1) -- -n > "$$prelog" 2>&1; then \
		cat "$$prelog"; \
		rm -f "$$prelog"; \
		exit 1; \
	fi; \
	if grep -q "ninja: no work to do." "$$prelog"; then \
		rm -f "$$prelog"; \
		out=$$(mktemp); \
		$(CMAKE) --build $(1) >"$$out" 2>&1; \
		r=$$?; \
		if [ $$r -ne 0 ]; then cat "$$out"; fi; \
		rm -f "$$out"; \
		exit $$r; \
	fi; \
	rm -f "$$prelog"; \
	out=$$(mktemp); \
	$(CMAKE) --build $(1) 2>"$$out"; \
	r=$$?; \
	if [ -s "$$out" ]; then grep -v 'skipping incompatible' < "$$out"; fi; \
	rm -f "$$out"; \
	if [ $$r -ne 0 ]; then \
		exit 1; \
	fi; \
	ver=$$(date +%s); \
	$(2); \
	log=$$(mktemp); \
	if ! $(CMAKE) --build $(1) > "$$log" 2>&1; then \
		cat "$$log"; \
		rm -f "$$log"; \
		exit 1; \
	fi; \
	rm -f "$$log"; \
	:
endef

HOST_ARCH := $(shell uname -m)
HOST_SYSTEM := $(shell uname -s)
NATIVE_ARCH := unsupported
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

NATIVE_TARGET := $(NATIVE_ARCH)/$(NATIVE_PLATFORM)
NATIVE_SHARED_NAME := librdv.so
NATIVE_IMPORT_LIBRARY :=
ifeq ($(NATIVE_PLATFORM),windows)
NATIVE_SHARED_NAME := librdv.dll
NATIVE_IMPORT_LIBRARY := -DRDV_TEST_IMPORT_LIBRARY=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/librdv.dll.a
endif
ifeq ($(NATIVE_PLATFORM),macos)
NATIVE_SHARED_NAME := librdv.dylib
endif

.DEFAULT_GOAL := $(NATIVE_TARGET)

.PHONY: all test wine wasm clean \
	x86_64/linux x86_64/windows x86_64/macos x86_64/iossim \
	i686/linux i686/windows \
	aarch64/linux aarch64/android aarch64/macos aarch64/ios aarch64/iossim \
	armv7/linux armv7/android armv7hf/linux \
	riscv64/linux powerpc64le/linux \
	mips/linux mipsel/linux mips64el/linux s390x/linux loongarch64/linux \
	wasm32/wasm

all: \
	x86_64/linux x86_64/windows x86_64/macos x86_64/iossim \
	i686/linux i686/windows \
	aarch64/linux aarch64/android aarch64/macos aarch64/ios aarch64/iossim \
	armv7/linux armv7/android armv7hf/linux \
	riscv64/linux powerpc64le/linux \
	mips/linux mipsel/linux mips64el/linux s390x/linux loongarch64/linux \
	wasm32/wasm

## Linux

define linux_target
	@mkdir -p $(BIN_DIR)/$(1)/linux
	@if [ ! -f $(BUILD_DIR)/$(subst /,-,$(1))-linux/CMakeCache.txt ] || grep -q '^RDV_BUILD_VERSION:.*=0$$' $(BUILD_DIR)/$(subst /,-,$(1))-linux/CMakeCache.txt; then \
		$(CMAKE) -S . -B $(BUILD_DIR)/$(subst /,-,$(1))-linux \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_SYSTEM_NAME=Linux \
		-DRDV_BUILD_VERSION=$(BUILD_VERSION) \
		-DCMAKE_C_COMPILER=$(2) \
		-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux \
		-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux \
		-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(subst /,-,$(1))-linux,$(CMAKE) -S . -B $(BUILD_DIR)/$(subst /,-,$(1))-linux -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Linux -DRDV_BUILD_VERSION=$$ver -DCMAKE_C_COMPILER=$(2) -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux -G Ninja -Wno-dev > /dev/null)
	@echo "OK $(1)/linux"
endef

x86_64/linux: ; $(call linux_target,x86_64,gcc)
i686/linux: ; $(call linux_target,i686,i686-linux-gnu-gcc)
aarch64/linux: ; $(call linux_target,aarch64,aarch64-linux-gnu-gcc)
armv7/linux: ; $(call linux_target,armv7,arm-linux-gnueabi-gcc)
armv7hf/linux: ; $(call linux_target,armv7hf,arm-linux-gnueabihf-gcc)
riscv64/linux: ; $(call linux_target,riscv64,riscv64-linux-gnu-gcc)
powerpc64le/linux: ; $(call linux_target,powerpc64le,powerpc64le-linux-gnu-gcc)
mips/linux: ; $(call linux_target,mips,mips-linux-gnu-gcc)
mipsel/linux: ; $(call linux_target,mipsel,mipsel-linux-gnu-gcc)
mips64el/linux: ; $(call linux_target,mips64el,mips64el-linux-gnuabi64-gcc)
s390x/linux: ; $(call linux_target,s390x,s390x-linux-gnu-gcc)
loongarch64/linux: ; $(call linux_target,loongarch64,loongarch64-linux-gnu-gcc)

## Windows

define windows_target
	@mkdir -p $(BIN_DIR)/$(1)/windows
	@if [ ! -f $(BUILD_DIR)/$(1)-windows/CMakeCache.txt ] || grep -q '^RDV_BUILD_VERSION:.*=0$$' $(BUILD_DIR)/$(1)-windows/CMakeCache.txt; then \
		$(CMAKE) -S . -B $(BUILD_DIR)/$(1)-windows \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_SYSTEM_NAME=Windows \
		-DRDV_BUILD_VERSION=$(BUILD_VERSION) \
		-DCMAKE_C_COMPILER=$(2) \
		-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-windows/out \
		-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows \
		-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows \
		-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-windows,$(CMAKE) -S . -B $(BUILD_DIR)/$(1)-windows -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Windows -DRDV_BUILD_VERSION=$$ver -DCMAKE_C_COMPILER=$(2) -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-windows/out -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows -G Ninja -Wno-dev > /dev/null)
	@cp $(BUILD_DIR)/$(1)-windows/out/librdv.dll $(BIN_DIR)/$(1)/windows/librdv.dll
	@echo "OK $(1)/windows"
endef

x86_64/windows: ; $(call windows_target,x86_64,x86_64-w64-mingw32-gcc)
i686/windows: ; $(call windows_target,i686,i686-w64-mingw32-gcc)

## Android

define android_target
	@if [ ! -f "$(NDK_TOOLCHAIN)" ]; then \
		echo "Missing Android NDK toolchain: $(NDK_TOOLCHAIN)" >&2; exit 1; \
	fi
	@mkdir -p $(BIN_DIR)/$(1)/android
	@if [ ! -f $(BUILD_DIR)/$(1)-android/CMakeCache.txt ] || grep -q '^RDV_BUILD_VERSION:.*=0$$' $(BUILD_DIR)/$(1)-android/CMakeCache.txt; then \
		$(CMAKE) -S . -B $(BUILD_DIR)/$(1)-android \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE=$(NDK_TOOLCHAIN) \
		-DANDROID_ABI=$(2) \
		-DANDROID_PLATFORM=android-24 \
		-DRDV_BUILD_VERSION=$(BUILD_VERSION) \
		-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/android \
		-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/android \
		-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-android,$(CMAKE) -S . -B $(BUILD_DIR)/$(1)-android -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$(NDK_TOOLCHAIN) -DANDROID_ABI=$(2) -DANDROID_PLATFORM=android-24 -DRDV_BUILD_VERSION=$$ver -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/android -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/android -G Ninja -Wno-dev > /dev/null)
	@echo "OK $(1)/android"
endef

aarch64/android: ; $(call android_target,aarch64,arm64-v8a)
armv7/android: ; $(call android_target,armv7,armeabi-v7a)

## macOS

define macos_target
	@if [ ! -x "$(2)" ]; then echo "Missing macOS compiler: $(2)" >&2; exit 1; fi
	@if [ -z "$(MACOSX_SDK)" ] || [ ! -d "$(MACOSX_SDK)" ]; then \
		echo "Missing macOS SDK under $(OSXCROSS_ROOT)" >&2; exit 1; \
	fi
	@mkdir -p $(BIN_DIR)/$(1)/macos
	@if [ ! -f $(BUILD_DIR)/$(1)-macos/CMakeCache.txt ] || grep -q '^RDV_BUILD_VERSION:.*=0$$' $(BUILD_DIR)/$(1)-macos/CMakeCache.txt; then \
		$(CMAKE) -S . -B $(BUILD_DIR)/$(1)-macos \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_SYSTEM_NAME=Darwin \
		-DCMAKE_OSX_SYSROOT=$(MACOSX_SDK) \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=$(MACOSX_DEPLOYMENT_TARGET) \
		-DCMAKE_C_COMPILER=$(2) \
		-DRDV_BUILD_VERSION=$(BUILD_VERSION) \
		-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/macos \
		-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/macos \
		-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-macos,$(CMAKE) -S . -B $(BUILD_DIR)/$(1)-macos -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Darwin -DCMAKE_OSX_SYSROOT=$(MACOSX_SDK) -DCMAKE_OSX_DEPLOYMENT_TARGET=$(MACOSX_DEPLOYMENT_TARGET) -DCMAKE_C_COMPILER=$(2) -DRDV_BUILD_VERSION=$$ver -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/macos -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/macos -G Ninja -Wno-dev > /dev/null)
	@echo "OK $(1)/macos"
endef

x86_64/macos: ; $(call macos_target,x86_64,$(OSXCROSS_X86_64_CC))
aarch64/macos: ; $(call macos_target,aarch64,$(OSXCROSS_AARCH64_CC))

## iOS

define ios_target
	@if [ ! -x "$(3)" ]; then echo "Missing iOS compiler: $(3)" >&2; exit 1; fi
	@if [ -z "$(4)" ] || [ ! -d "$(4)" ]; then echo "Missing iOS SDK" >&2; exit 1; fi
	@mkdir -p $(BIN_DIR)/$(1)/$(2)
	@if [ ! -f $(BUILD_DIR)/$(1)-$(2)/CMakeCache.txt ] || grep -q '^RDV_BUILD_VERSION:.*=0$$' $(BUILD_DIR)/$(1)-$(2)/CMakeCache.txt; then \
		$(CMAKE) -S . -B $(BUILD_DIR)/$(1)-$(2) \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_SYSTEM_NAME=iOS \
		-DCMAKE_OSX_SYSROOT=$(4) \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
		-DCMAKE_C_COMPILER=$(3) \
		-DRDV_BUILD_VERSION=$(BUILD_VERSION) \
		-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/$(2) \
		-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/$(2) \
		-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-$(2),$(CMAKE) -S . -B $(BUILD_DIR)/$(1)-$(2) -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=$(4) -DCMAKE_OSX_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) -DCMAKE_C_COMPILER=$(3) -DRDV_BUILD_VERSION=$$ver -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/$(2) -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/$(2) -G Ninja -Wno-dev > /dev/null)
	@echo "OK $(1)/$(2)"
endef

aarch64/ios: ; $(call ios_target,aarch64,ios,$(OSXCROSS_IOS_AARCH64_CC),$(IPHONEOS_SDK))
aarch64/iossim: ; $(call ios_target,aarch64,iossim,$(OSXCROSS_IOSSIM_AARCH64_CC),$(IPHONESIMULATOR_SDK))
x86_64/iossim: ; $(call ios_target,x86_64,iossim,$(OSXCROSS_IOSSIM_X86_64_CC),$(IPHONESIMULATOR_SDK))

## WebAssembly

wasm32/wasm:
	@if ! command -v $(WASM_EMCMAKE) >/dev/null 2>&1; then \
		echo "Missing Emscripten cmake wrapper: $(WASM_EMCMAKE)" >&2; exit 1; \
	fi
	@mkdir -p $(BIN_DIR)/wasm32/wasm
	@if [ ! -f $(BUILD_DIR)/wasm32-wasm/CMakeCache.txt ] || grep -q '^RDV_BUILD_VERSION:.*=0$$' $(BUILD_DIR)/wasm32-wasm/CMakeCache.txt; then \
		$(WASM_EMCMAKE) $(CMAKE) -S . -B $(BUILD_DIR)/wasm32-wasm \
		-DCMAKE_BUILD_TYPE=Release \
		-DRDV_BUILD_VERSION=$(BUILD_VERSION) \
		-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/wasm32/wasm \
		-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/wasm32-wasm,wasm_log=$$(mktemp); if ! $(WASM_EMCMAKE) $(CMAKE) -S . -B $(BUILD_DIR)/wasm32-wasm -DCMAKE_BUILD_TYPE=Release -DRDV_BUILD_VERSION=$$ver -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/wasm32/wasm -G Ninja -Wno-dev > "$$wasm_log" 2>&1; then cat "$$wasm_log"; rm -f "$$wasm_log"; exit 1; fi; rm -f "$$wasm_log")
	@echo "OK wasm32/wasm"

## Tests

test:
	@if [ -n "$(filter wine,$(MAKECMDGOALS))" ]; then \
		if ! command -v $(WINE) >/dev/null 2>&1; then echo "Missing Wine runtime: $(WINE)" >&2; exit 1; fi; \
		if [ ! -f $(BIN_DIR)/x86_64/windows/librdv.dll ]; then echo "Run 'make x86_64/windows' first." >&2; exit 1; fi; \
		cmake -S . -B $(BUILD_DIR)/test-wine \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Windows \
			-DCMAKE_C_COMPILER=$(WINE_X86_64_CC) \
			-DRDV_BUILD_TESTS=ON \
			-DRDV_BUILD_VERSION=$(BUILD_VERSION) \
			-DRDV_TEST_SHARED_LIBRARY=$(CURDIR)/$(BIN_DIR)/x86_64/windows/librdv.dll \
			-DRDV_TEST_IMPORT_LIBRARY=$(CURDIR)/$(BIN_DIR)/x86_64/windows/librdv.dll.a \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/test-wine/out \
			-G Ninja -Wno-dev > /dev/null; \
		cmake --build $(BUILD_DIR)/test-wine --target rdv_contract_test; \
		WINEDEBUG=-all $(WINE) $(BUILD_DIR)/test-wine/out/rdv_contract_test.exe all; \
	elif [ -n "$(filter wasm,$(MAKECMDGOALS))" ]; then \
		if ! command -v $(WASM_EMCMAKE) >/dev/null 2>&1; then echo "Missing Emscripten cmake wrapper" >&2; exit 1; fi; \
		if ! command -v $(WASM_NODE) >/dev/null 2>&1; then echo "Missing Node.js runtime" >&2; exit 1; fi; \
		if [ ! -f $(BUILD_DIR)/test-wasm/CMakeCache.txt ] || grep -q '^RDV_BUILD_VERSION:.*=0$$' $(BUILD_DIR)/test-wasm/CMakeCache.txt; then \
			$(WASM_EMCMAKE) cmake -S . -B $(BUILD_DIR)/test-wasm \
				-DCMAKE_BUILD_TYPE=Release \
				-DRDV_BUILD_TESTS=ON \
				-DRDV_BUILD_VERSION=$(BUILD_VERSION) \
				-G Ninja -Wno-dev > /dev/null; \
		fi; \
		cmake --build $(BUILD_DIR)/test-wasm --target rdv_contract_test; \
		$(WASM_NODE) $(BUILD_DIR)/test-wasm/rdv_contract_test.js all; \
	else \
		cmake -S . -B $(BUILD_DIR)/test \
			-DCMAKE_BUILD_TYPE=Release \
			-DRDV_BUILD_TESTS=ON \
			-DRDV_BUILD_VERSION=$(BUILD_VERSION) \
			-G Ninja -Wno-dev > /dev/null; \
		cmake --build $(BUILD_DIR)/test --target rdv_contract_test; \
		$(BUILD_DIR)/test/rdv_contract_test all; \
	fi

wine:
	@:

wasm:
	@:

clean:
	@rm -rf $(BUILD_DIR) $(BIN_DIR)
