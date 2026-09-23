## Makefile
## Summary: Cross-compilation builder for mmap.
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
OSXCROSS_AARCH64_CC := $(OSXCROSS_ROOT)/bin/oa64-clang
OSXCROSS_IOS_AARCH64_CC := $(OSXCROSS_ROOT)/bin/ios64-clang
OSXCROSS_IOSSIM_AARCH64_CC := $(OSXCROSS_ROOT)/bin/iossim64-clang
OSXCROSS_IOSSIM_X86_64_CC := $(OSXCROSS_ROOT)/bin/iossimx64-clang
WINE ?= wine
WINE_X86_64_CC ?= x86_64-w64-mingw32-gcc
WASM_EMCMAKE ?= emcmake
WASM_EMCC    ?= emcc
WASM_NODE    ?= node

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
		$(2); \
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

ifneq ($(filter MINGW% MSYS% CYGWIN%,$(HOST_SYSTEM)),)
NATIVE_PLATFORM := windows
endif

NATIVE_TARGET := $(NATIVE_ARCH)/$(NATIVE_PLATFORM)
NATIVE_EXE_EXT :=
NATIVE_SHARED_NAME := libmmap.so
NATIVE_IMPORT_LIBRARY :=

ifeq ($(NATIVE_PLATFORM),windows)
NATIVE_EXE_EXT := .exe
NATIVE_SHARED_NAME := libmmap.dll
NATIVE_IMPORT_LIBRARY := -DMMAP_TEST_IMPORT_LIBRARY=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/libmmap.dll.a
endif

.DEFAULT_GOAL := $(NATIVE_TARGET)

.PHONY: all test wine wasm clean \
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
	wasm32/wasm

all: \
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
	wasm32/wasm

## Linux

define linux_target
	@mkdir -p $(BIN_DIR)/$(1)/linux
	@cache=$(BUILD_DIR)/$(subst /,-,$(1))-linux/CMakeCache.txt; \
	if [ -f "$$cache" ] && ! grep -Eq '^MMAP_BUILD_VERSION:.*=[0-9]+$$' "$$cache"; then \
		rm -f "$$cache" "${cache%/*}/build.ninja" && rm -rf "${cache%/*}/CMakeFiles"; \
	fi
	@if [ ! -f $(BUILD_DIR)/$(subst /,-,$(1))-linux/CMakeCache.txt ]; then \
		cmake -S . -B $(BUILD_DIR)/$(subst /,-,$(1))-linux \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Linux \
			-DMMAP_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_C_COMPILER=$(2) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(subst /,-,$(1))-linux/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(subst /,-,$(1))-linux,cmake -S . -B $(BUILD_DIR)/$(subst /,-,$(1))-linux -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Linux -DMMAP_BUILD_VERSION=$$ver -DCMAKE_C_COMPILER=$(2) -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(subst /,-,$(1))-linux/out -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux -G Ninja -Wno-dev > /dev/null)
	@cp $(BUILD_DIR)/$(subst /,-,$(1))-linux/out/mmap $(BIN_DIR)/$(1)/linux/mmap.tmp
	@mv -f $(BIN_DIR)/$(1)/linux/mmap.tmp $(BIN_DIR)/$(1)/linux/mmap
	@echo "OK $(1)/linux"
endef

x86_64/linux:
	$(call linux_target,x86_64,gcc)

i686/linux:
	$(call linux_target,i686,i686-linux-gnu-gcc)

aarch64/linux:
	$(call linux_target,aarch64,aarch64-linux-gnu-gcc)

armv7hf/linux:
	$(call linux_target,armv7hf,arm-linux-gnueabihf-gcc)

armv7/linux:
	$(call linux_target,armv7,arm-linux-gnueabi-gcc)

riscv64/linux:
	$(call linux_target,riscv64,riscv64-linux-gnu-gcc)

powerpc64le/linux:
	$(call linux_target,powerpc64le,powerpc64le-linux-gnu-gcc)

mips/linux:
	$(call linux_target,mips,mips-linux-gnu-gcc)

mipsel/linux:
	$(call linux_target,mipsel,mipsel-linux-gnu-gcc)

mips64el/linux:
	$(call linux_target,mips64el,mips64el-linux-gnuabi64-gcc)

s390x/linux:
	$(call linux_target,s390x,s390x-linux-gnu-gcc)

loongarch64/linux:
	$(call linux_target,loongarch64,loongarch64-linux-gnu-gcc)

## Windows

define windows_target
	@mkdir -p $(BIN_DIR)/$(1)/windows
	@cache=$(BUILD_DIR)/$(1)-windows/CMakeCache.txt; \
	if [ -f "$$cache" ] && ! grep -Eq '^MMAP_BUILD_VERSION:.*=[0-9]+$$' "$$cache"; then \
		rm -f "$$cache" "${cache%/*}/build.ninja" && rm -rf "${cache%/*}/CMakeFiles"; \
	fi
	@if [ ! -f $(BUILD_DIR)/$(1)-windows/CMakeCache.txt ]; then \
		cmake -S . -B $(BUILD_DIR)/$(1)-windows \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Windows \
			-DMMAP_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_C_COMPILER=$(2) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-windows/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-windows,cmake -S . -B $(BUILD_DIR)/$(1)-windows -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Windows -DMMAP_BUILD_VERSION=$$ver -DCMAKE_C_COMPILER=$(2) -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-windows/out -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows -G Ninja -Wno-dev > /dev/null)
	@cp $(BUILD_DIR)/$(1)-windows/out/mmap.exe $(BIN_DIR)/$(1)/windows/mmap.exe.tmp
	@mv -f $(BIN_DIR)/$(1)/windows/mmap.exe.tmp $(BIN_DIR)/$(1)/windows/mmap.exe
	@cp $(BUILD_DIR)/$(1)-windows/out/libmmap.dll $(BIN_DIR)/$(1)/windows/libmmap.dll.tmp
	@mv -f $(BIN_DIR)/$(1)/windows/libmmap.dll.tmp $(BIN_DIR)/$(1)/windows/libmmap.dll
	@echo "OK $(1)/windows"
endef

x86_64/windows:
	$(call windows_target,x86_64,x86_64-w64-mingw32-gcc)

i686/windows:
	$(call windows_target,i686,i686-w64-mingw32-gcc)

## macOS

define macos_target
	@mkdir -p $(BIN_DIR)/$(1)/macos
	@if ! $(2) --version > /dev/null 2>&1; then \
		echo "Missing macOS cross-compiler wrapper: $(2)" >&2; \
		echo "Set OSXCROSS_ROOT to your osxcross target dir and ensure the wrappers are built." >&2; \
		exit 1; \
	fi
	@export OSXCROSS_HOST=$(1)-apple-darwin25.1 && \
	export OSXCROSS_TARGET_DIR=$(OSXCROSS_ROOT) && \
	export OSXCROSS_TARGET=darwin25.1 && \
	export OSXCROSS_SDK=$(MACOSX_SDK) && \
	export LD_LIBRARY_PATH="$(OSXCROSS_ROOT)/lib:$${LD_LIBRARY_PATH:-}" && \
	export PATH="$(OSXCROSS_ROOT)/bin:$$PATH" && \
	cache=$(BUILD_DIR)/$(1)-macos/CMakeCache.txt && \
	if [ -f "$$cache" ] && ! grep -q '^CMAKE_C_COMPILER:.*=$(2)$$' "$$cache"; then \
		rm -f "$$cache" $(BUILD_DIR)/$(1)-macos/build.ninja && rm -rf $(BUILD_DIR)/$(1)-macos/CMakeFiles; \
	fi && \
	if [ ! -f $(BUILD_DIR)/$(1)-macos/build.ninja ]; then \
		cmake -S . -B $(BUILD_DIR)/$(1)-macos \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Darwin \
			-DCMAKE_OSX_DEPLOYMENT_TARGET=$(MACOSX_DEPLOYMENT_TARGET) \
			-DMMAP_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_C_COMPILER=$(2) \
			-DCMAKE_AR=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ar \
			-DCMAKE_RANLIB=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ranlib \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-macos/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/macos \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/macos \
			-G Ninja -Wno-dev > /dev/null; \
	fi && \
	cmake --build $(BUILD_DIR)/$(1)-macos 2>&1 && \
	cp $(BUILD_DIR)/$(1)-macos/out/mmap $(BIN_DIR)/$(1)/macos/mmap && \
	echo "OK $(1)/macos"
endef

x86_64/macos:
	$(call macos_target,x86_64,$(OSXCROSS_X86_64_CC))

aarch64/macos:
	$(call macos_target,aarch64,$(OSXCROSS_AARCH64_CC))

## iOS

define ios_target
	@mkdir -p $(BIN_DIR)/$(1)/$(2)
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
	@export OSXCROSS_HOST=$(1)-apple-darwin25.1 && \
	export OSXCROSS_TARGET_DIR=$(OSXCROSS_ROOT) && \
	export OSXCROSS_TARGET=darwin25.1 && \
	export OSXCROSS_SDK=$(MACOSX_SDK) && \
	export LD_LIBRARY_PATH="$(OSXCROSS_ROOT)/lib:$${LD_LIBRARY_PATH:-}" && \
	export PATH="$(OSXCROSS_ROOT)/bin:$$PATH" && \
	cache=$(BUILD_DIR)/$(1)-$(2)/CMakeCache.txt && \
	if [ -f "$$cache" ] && { ! grep -q '^CMAKE_C_COMPILER:.*=$(3)$$' "$$cache" || ! grep -q '^CMAKE_OSX_SYSROOT:.*=$(5)$$' "$$cache"; }; then \
		rm -f "$$cache" $(BUILD_DIR)/$(1)-$(2)/build.ninja && rm -rf $(BUILD_DIR)/$(1)-$(2)/CMakeFiles; \
	fi && \
	if [ ! -f $(BUILD_DIR)/$(1)-$(2)/build.ninja ]; then \
		cmake -S . -B $(BUILD_DIR)/$(1)-$(2) \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=iOS \
			-DCMAKE_SYSTEM_VERSION=$(IOS_DEPLOYMENT_TARGET) \
			-DCMAKE_OSX_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
			-DCMAKE_OSX_SYSROOT=$(5) \
			-DCMAKE_OSX_ARCHITECTURES=$(6) \
			-DMMAP_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_C_COMPILER=$(3) \
			-DCMAKE_AR=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ar \
			-DCMAKE_RANLIB=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ranlib \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-$(2)/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/$(2) \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/$(2) \
			-G Ninja -Wno-dev > /dev/null; \
	fi && \
	cmake --build $(BUILD_DIR)/$(1)-$(2) 2>&1 && \
	if [ -f $(BUILD_DIR)/$(1)-$(2)/out/mmap ]; then \
		cp $(BUILD_DIR)/$(1)-$(2)/out/mmap $(BIN_DIR)/$(1)/$(2)/mmap; \
	elif [ -f $(BUILD_DIR)/$(1)-$(2)/out/mmap.app/mmap ]; then \
		cp $(BUILD_DIR)/$(1)-$(2)/out/mmap.app/mmap $(BIN_DIR)/$(1)/$(2)/mmap; \
	else \
		echo "Missing built iOS executable for $(1)/$(2)" >&2; \
		exit 1; \
	fi && \
	echo "OK $(1)/$(2)"
endef

aarch64/ios:
	$(call ios_target,aarch64,ios,$(OSXCROSS_IOS_AARCH64_CC),IPHONEOS_SDK,$(IPHONEOS_SDK),arm64)

aarch64/iossim:
	$(call ios_target,aarch64,iossim,$(OSXCROSS_IOSSIM_AARCH64_CC),IPHONESIMULATOR_SDK,$(IPHONESIMULATOR_SDK),arm64)

x86_64/iossim:
	$(call ios_target,x86_64,iossim,$(OSXCROSS_IOSSIM_X86_64_CC),IPHONESIMULATOR_SDK,$(IPHONESIMULATOR_SDK),x86_64)

## Android

define android_target
	@mkdir -p $(BIN_DIR)/$(1)/android
	@cache=$(BUILD_DIR)/$(1)-android/CMakeCache.txt; \
	if [ -f "$$cache" ] && ! grep -Eq '^MMAP_BUILD_VERSION:.*=[0-9]+$$' "$$cache"; then \
		rm -f "$$cache" "${cache%/*}/build.ninja" && rm -rf "${cache%/*}/CMakeFiles"; \
	fi
	@cache=$(BUILD_DIR)/$(1)-android/CMakeCache.txt && \
	if [ -f "$$cache" ] && { ! grep -q '^CMAKE_TOOLCHAIN_FILE:.*=$(NDK_TOOLCHAIN)$$' "$$cache" || ! grep -q '^ANDROID_ABI:.*=$(2)$$' "$$cache"; }; then \
		rm -f "$$cache" $(BUILD_DIR)/$(1)-android/build.ninja && rm -rf $(BUILD_DIR)/$(1)-android/CMakeFiles; \
	fi
	@if [ ! -f $(BUILD_DIR)/$(1)-android/CMakeCache.txt ]; then \
		cmake -S . -B $(BUILD_DIR)/$(1)-android \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_TOOLCHAIN_FILE=$(NDK_TOOLCHAIN) \
			-DMMAP_BUILD_VERSION=$(BUILD_VERSION) \
			-DANDROID_ABI=$(2) \
			-DANDROID_PLATFORM=android-21 \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-android/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/android \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/android \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-android,cmake -S . -B $(BUILD_DIR)/$(1)-android -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$(NDK_TOOLCHAIN) -DMMAP_BUILD_VERSION=$$ver -DANDROID_ABI=$(2) -DANDROID_PLATFORM=android-21 -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-android/out -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/android -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/android -G Ninja -Wno-dev > /dev/null)
	@cp $(BUILD_DIR)/$(1)-android/out/mmap $(BIN_DIR)/$(1)/android/mmap.tmp
	@mv -f $(BIN_DIR)/$(1)/android/mmap.tmp $(BIN_DIR)/$(1)/android/mmap
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
	if [ -f "$$cache" ] && ! grep -Eq '^MMAP_BUILD_VERSION:.*=[0-9]+$$' "$$cache"; then \
		$(WASM_EMCMAKE) cmake -S . -B $(BUILD_DIR)/wasm32-wasm \
			-DCMAKE_BUILD_TYPE=Release \
			-DMMAP_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/wasm32-wasm/out \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	@if [ ! -f $(BUILD_DIR)/wasm32-wasm/CMakeCache.txt ]; then \
		$(WASM_EMCMAKE) cmake -S . -B $(BUILD_DIR)/wasm32-wasm \
			-DCMAKE_BUILD_TYPE=Release \
			-DMMAP_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/wasm32-wasm/out \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/wasm32-wasm,cmake -S . -B $(BUILD_DIR)/wasm32-wasm -DCMAKE_BUILD_TYPE=Release -DMMAP_BUILD_VERSION=$$ver -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/wasm32-wasm/out -G Ninja -Wno-dev > /dev/null)
	@cp $(BUILD_DIR)/wasm32-wasm/out/mmap.wasm $(BIN_DIR)/wasm32/wasm/mmap.wasm
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
		if [ ! -f $(BIN_DIR)/x86_64/windows/libmmap.dll ] || [ ! -f $(BIN_DIR)/x86_64/windows/libmmap.dll.a ] || [ ! -f $(BIN_DIR)/x86_64/windows/mmap.exe ]; then \
			echo "Missing Windows artifacts. Run 'make x86_64/windows' or 'make all' first." >&2; \
			exit 1; \
		fi; \
		if [ ! -f $(BUILD_DIR)/test-wine/CMakeCache.txt ]; then \
			cmake -S . -B $(BUILD_DIR)/test-wine \
				-DCMAKE_BUILD_TYPE=Release \
				-DCMAKE_SYSTEM_NAME=Windows \
				-DCMAKE_C_COMPILER=$(WINE_X86_64_CC) \
				-DMMAP_BUILD_TESTS=ON \
				-DMMAP_BUILD_VERSION=$(BUILD_VERSION) \
				-DMMAP_TEST_SHARED_LIBRARY=$(CURDIR)/$(BIN_DIR)/x86_64/windows/libmmap.dll \
				-DMMAP_TEST_IMPORT_LIBRARY=$(CURDIR)/$(BIN_DIR)/x86_64/windows/libmmap.dll.a \
				-DMMAP_TEST_CLI=$(CURDIR)/$(BIN_DIR)/x86_64/windows/mmap.exe \
				-DCMAKE_CROSSCOMPILING_EMULATOR=$(WINE) \
				-G Ninja -Wno-dev > /dev/null; \
		fi; \
		cmake --build $(BUILD_DIR)/test-wine --target mmap_contract_test || exit 1; \
		EGL_LOG_LEVEL=fatal WINEDEBUG=-all $(WINE) $(BUILD_DIR)/test-wine/mmap_contract_test.exe all; \
	elif [ -n "$(filter wasm,$(MAKECMDGOALS))" ]; then \
		if ! command -v $(WASM_EMCMAKE) >/dev/null 2>&1 || ! command -v $(WASM_EMCC) >/dev/null 2>&1; then \
			echo "Missing Emscripten SDK toolchain: $(WASM_EMCMAKE)/$(WASM_EMCC)" >&2; \
			echo "Install the Emscripten SDK and add emcmake/emcc to PATH (e.g. 'source emsdk_env.sh')." >&2; \
			exit 1; \
		fi; \
		if [ ! -f $(BIN_DIR)/wasm32/wasm/mmap.wasm ]; then \
			echo "Missing $(BIN_DIR)/wasm32/wasm/mmap.wasm. Run 'make wasm32/wasm' first." >&2; \
			exit 1; \
		fi; \
		if [ ! -f $(BUILD_DIR)/test-wasm/CMakeCache.txt ]; then \
			$(WASM_EMCMAKE) cmake -S . -B $(BUILD_DIR)/test-wasm \
				-DCMAKE_BUILD_TYPE=Release \
				-DMMAP_BUILD_TESTS=ON \
				-DMMAP_BUILD_VERSION=$(BUILD_VERSION) \
				-G Ninja -Wno-dev > /dev/null; \
		fi; \
		cmake --build $(BUILD_DIR)/test-wasm --target mmap_contract_test || exit 1; \
		if [ -n "$(EMSDK_NODE)" ]; then node="$(EMSDK_NODE)"; else node="$(WASM_NODE)"; fi; \
		if ! command -v "$$node" >/dev/null 2>&1; then \
			echo "Missing Emscripten test runtime: $node" >&2; \
			exit 1; \
		fi; \
		"$$node" $(BUILD_DIR)/test-wasm/mmap_contract_test.js all; \
	else \
		if [ "$(NATIVE_ARCH)" = "unsupported" ] || [ "$(NATIVE_PLATFORM)" = "unsupported" ]; then \
			echo "Unsupported native test target $(HOST_ARCH)/$(HOST_SYSTEM)" >&2; \
			exit 1; \
		fi; \
		if [ ! -f $(BIN_DIR)/$(NATIVE_TARGET)/$(NATIVE_SHARED_NAME) ] || [ ! -f $(BIN_DIR)/$(NATIVE_TARGET)/mmap$(NATIVE_EXE_EXT) ]; then \
			echo "Missing native artifacts. Run 'make' first." >&2; \
			exit 1; \
		fi; \
		if [ ! -f $(BUILD_DIR)/test/CMakeCache.txt ]; then \
			cmake -S . -B $(BUILD_DIR)/test \
				-DCMAKE_BUILD_TYPE=Release \
				-DMMAP_BUILD_TESTS=ON \
				-DMMAP_BUILD_VERSION=$(BUILD_VERSION) \
				-DMMAP_TEST_SHARED_LIBRARY=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/$(NATIVE_SHARED_NAME) \
				-DMMAP_TEST_CLI=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/mmap$(NATIVE_EXE_EXT) \
				$(NATIVE_IMPORT_LIBRARY) \
				-G Ninja -Wno-dev > /dev/null; \
		fi; \
		cmake --build $(BUILD_DIR)/test --target mmap_contract_test || exit 1; \
		$(BUILD_DIR)/test/mmap_contract_test all; \
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
