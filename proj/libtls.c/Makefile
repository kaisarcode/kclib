## Makefile
## Summary: Cross-compilation builder for tls.
##
## Author: KaisarCode
## Website: https://kaisarcode.com
## License: https://www.gnu.org/licenses/lgpl-3.0.html

ANDROID_HOME ?= $(HOME)/.local/share/android-sdk
NDK_VERSION ?= 27.2.12479018
NDK_DIR := $(ANDROID_HOME)/ndk/$(NDK_VERSION)
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

BUILD_DIR := .build
BIN_DIR := bin
BUILD_VERSION ?= $(shell date +%s)
CMAKE ?= cmake

define cmake_build
	@prelog=$$(mktemp); \
	if ! $(3) $(CMAKE) --build $(1) -- -n > "$$prelog" 2>&1; then \
		cat "$$prelog"; \
		rm -f "$$prelog"; \
		exit 1; \
	fi; \
	if grep -q "ninja: no work to do." "$$prelog"; then \
		rm -f "$$prelog"; \
		out=$$(mktemp); \
		$(3) $(CMAKE) --build $(1) 2>"$$out"; \
		r=$$?; \
		if [ -s "$$out" ]; then grep -v 'skipping incompatible' < "$$out"; fi; \
		rm -f "$$out"; \
		exit $$r; \
	fi; \
	rm -f "$$prelog"; \
	out=$$(mktemp); \
	$(3) $(CMAKE) --build $(1) 2>"$$out"; \
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
		if ! $(3) $(CMAKE) --build $(1) > "$$log" 2>&1; then \
			cat "$$log"; \
			rm -f "$$log"; \
			exit 1; \
		fi; \
		rm -f "$$log"; \
	fi; \
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

NATIVE_TARGET := $(NATIVE_PLATFORM)/$(NATIVE_ARCH)
NATIVE_SHARED_NAME := libtls.so
NATIVE_CLI_NAME := tls

ifeq ($(NATIVE_PLATFORM),windows)
NATIVE_SHARED_NAME := libtls.dll
NATIVE_CLI_NAME := tls.exe
endif

ifeq ($(NATIVE_PLATFORM),macos)
NATIVE_SHARED_NAME := libtls.dylib
endif

.DEFAULT_GOAL := $(NATIVE_TARGET)

.PHONY: all test wine clean \
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
	linux/loongarch64

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
	linux/loongarch64

define linux_target
	@mkdir -p $(BIN_DIR)/linux/$(1)
	@if [ ! -f $(BUILD_DIR)/$(subst /,-,$(1))-linux/CMakeCache.txt ] || ! grep -Fq '$(CURDIR)/$(BIN_DIR)/linux/$(1)' $(BUILD_DIR)/$(subst /,-,$(1))-linux/CMakeCache.txt; then \
		$(CMAKE) -S . -B $(BUILD_DIR)/$(subst /,-,$(1))-linux \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Linux \
			-DCMAKE_SYSTEM_PROCESSOR=$(1) \
			-DCMAKE_C_COMPILER=$(2) \
			-DTLS_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(subst /,-,$(1))-linux/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/linux/$(1) \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/linux/$(1) \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(subst /,-,$(1))-linux,$(CMAKE) -S . -B $(BUILD_DIR)/$(subst /,-,$(1))-linux -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=$(1) -DCMAKE_C_COMPILER=$(2) -DTLS_BUILD_VERSION=$$ver -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(subst /,-,$(1))-linux/out -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/linux/$(1) -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/linux/$(1) -G Ninja -Wno-dev > /dev/null)
	@echo "OK linux/$(1)"
endef

linux/x86_64:
	$(call linux_target,x86_64,gcc)

linux/i686:
	$(call linux_target,i686,i686-linux-gnu-gcc)

linux/aarch64:
	$(call linux_target,aarch64,aarch64-linux-gnu-gcc)

linux/armv7hf:
	$(call linux_target,armv7hf,arm-linux-gnueabihf-gcc)

linux/armv7:
	$(call linux_target,armv7,arm-linux-gnueabi-gcc)

linux/riscv64:
	$(call linux_target,riscv64,riscv64-linux-gnu-gcc)

linux/powerpc64le:
	$(call linux_target,powerpc64le,powerpc64le-linux-gnu-gcc)

linux/mips:
	$(call linux_target,mips,mips-linux-gnu-gcc)

linux/mipsel:
	$(call linux_target,mipsel,mipsel-linux-gnu-gcc)

linux/mips64el:
	$(call linux_target,mips64el,mips64el-linux-gnuabi64-gcc)

linux/s390x:
	$(call linux_target,s390x,s390x-linux-gnu-gcc)

linux/loongarch64:
	$(call linux_target,loongarch64,loongarch64-linux-gnu-gcc)

define windows_target
	@mkdir -p $(BIN_DIR)/windows/$(1)
	@if [ ! -f $(BUILD_DIR)/$(1)-windows/CMakeCache.txt ] || ! grep -Fq '$(CURDIR)/$(BIN_DIR)/windows/$(1)' $(BUILD_DIR)/$(1)-windows/CMakeCache.txt; then \
		$(CMAKE) -S . -B $(BUILD_DIR)/$(1)-windows \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Windows \
			-DCMAKE_SYSTEM_PROCESSOR=$(1) \
			-DCMAKE_C_COMPILER=$(2) \
			-DTLS_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-windows/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/windows/$(1) \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/windows/$(1) \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-windows,$(CMAKE) -S . -B $(BUILD_DIR)/$(1)-windows -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=$(1) -DCMAKE_C_COMPILER=$(2) -DTLS_BUILD_VERSION=$$ver -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-windows/out -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/windows/$(1) -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/windows/$(1) -G Ninja -Wno-dev > /dev/null)
	@cp $(BUILD_DIR)/$(1)-windows/out/libtls.dll \
		$(BIN_DIR)/windows/$(1)/libtls.dll
	@echo "OK windows/$(1)"
endef

windows/x86_64:
	$(call windows_target,x86_64,x86_64-w64-mingw32-gcc)

windows/i686:
	$(call windows_target,i686,i686-w64-mingw32-gcc)

define macos_target
	@mkdir -p $(BIN_DIR)/macos/$(1)
	@if ! $(2) --version > /dev/null 2>&1; then \
		echo "Missing macOS cross-compiler wrapper: $(2)" >&2; \
		exit 1; \
	fi
	@if [ -f $(BUILD_DIR)/$(1)-macos/CMakeCache.txt ] && \
		! grep -q '^CMAKE_AR:.*=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ar$$' $(BUILD_DIR)/$(1)-macos/CMakeCache.txt; then \
		LD_LIBRARY_PATH="$(OSXCROSS_ROOT)/lib:$${LD_LIBRARY_PATH:-}" \
		PATH="$(OSXCROSS_ROOT)/bin:$$PATH" \
		$(CMAKE) -S . -B $(BUILD_DIR)/$(1)-macos \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Darwin \
			-DCMAKE_SYSTEM_PROCESSOR=$(1) \
			-DCMAKE_OSX_DEPLOYMENT_TARGET=$(MACOSX_DEPLOYMENT_TARGET) \
			-DCMAKE_OSX_SYSROOT=$(MACOSX_SDK) \
			-DCMAKE_C_COMPILER=$(2) \
			-DCMAKE_AR=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ar \
			-DCMAKE_RANLIB=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ranlib \
			-DTLS_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-macos/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/macos/$(1) \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/macos/$(1) \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	@if [ ! -f $(BUILD_DIR)/$(1)-macos/CMakeCache.txt ] || ! grep -Fq '$(CURDIR)/$(BIN_DIR)/macos/$(1)' $(BUILD_DIR)/$(1)-macos/CMakeCache.txt; then \
		LD_LIBRARY_PATH="$(OSXCROSS_ROOT)/lib:$${LD_LIBRARY_PATH:-}" \
		PATH="$(OSXCROSS_ROOT)/bin:$$PATH" \
		$(CMAKE) -S . -B $(BUILD_DIR)/$(1)-macos \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Darwin \
			-DCMAKE_SYSTEM_PROCESSOR=$(1) \
			-DCMAKE_OSX_DEPLOYMENT_TARGET=$(MACOSX_DEPLOYMENT_TARGET) \
			-DCMAKE_OSX_SYSROOT=$(MACOSX_SDK) \
			-DCMAKE_C_COMPILER=$(2) \
			-DCMAKE_AR=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ar \
			-DCMAKE_RANLIB=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ranlib \
			-DTLS_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-macos/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/macos/$(1) \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/macos/$(1) \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-macos,LD_LIBRARY_PATH="$(OSXCROSS_ROOT)/lib:$${LD_LIBRARY_PATH:-}" PATH="$(OSXCROSS_ROOT)/bin:$$PATH" $(CMAKE) -S . -B $(BUILD_DIR)/$(1)-macos -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Darwin -DCMAKE_SYSTEM_PROCESSOR=$(1) -DCMAKE_OSX_DEPLOYMENT_TARGET=$(MACOSX_DEPLOYMENT_TARGET) -DCMAKE_OSX_SYSROOT=$(MACOSX_SDK) -DCMAKE_C_COMPILER=$(2) -DCMAKE_AR=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ar -DCMAKE_RANLIB=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ranlib -DTLS_BUILD_VERSION=$$ver -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-macos/out -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/macos/$(1) -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/macos/$(1) -G Ninja -Wno-dev > /dev/null,LD_LIBRARY_PATH="$(OSXCROSS_ROOT)/lib:$${LD_LIBRARY_PATH:-}" PATH="$(OSXCROSS_ROOT)/bin:$$PATH")
	@echo "OK macos/$(1)"
endef

macos/x86_64:
	$(call macos_target,x86_64,$(OSXCROSS_X86_64_CC))

macos/aarch64:
	$(call macos_target,aarch64,$(OSXCROSS_AARCH64_CC))

define ios_target
	@mkdir -p $(BIN_DIR)/$(2)/$(1)
	@if ! $(3) --version > /dev/null 2>&1; then \
		echo "Missing iOS cross-compiler wrapper: $(3)" >&2; \
		exit 1; \
	fi
	@if [ -z "$(5)" ] || [ ! -d "$(5)" ]; then \
		echo "Missing iOS SDK sysroot: $(5)" >&2; \
		exit 1; \
	fi
	@if [ -f $(BUILD_DIR)/$(1)-$(2)/CMakeCache.txt ] && \
		! grep -q '^CMAKE_AR:.*=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ar$$' $(BUILD_DIR)/$(1)-$(2)/CMakeCache.txt; then \
		LD_LIBRARY_PATH="$(OSXCROSS_ROOT)/lib:$${LD_LIBRARY_PATH:-}" \
		PATH="$(OSXCROSS_ROOT)/bin:$$PATH" \
		$(CMAKE) -S . -B $(BUILD_DIR)/$(1)-$(2) \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=iOS \
			-DCMAKE_SYSTEM_VERSION=$(IOS_DEPLOYMENT_TARGET) \
			-DCMAKE_OSX_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
			-DCMAKE_OSX_SYSROOT=$(5) \
			-DCMAKE_OSX_ARCHITECTURES=$(6) \
			-DCMAKE_SYSTEM_PROCESSOR=$(1) \
			-DCMAKE_C_COMPILER=$(3) \
			-DCMAKE_AR=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ar \
			-DCMAKE_RANLIB=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ranlib \
			-DTLS_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-$(2)/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(2)/$(1) \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(2)/$(1) \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	@if [ ! -f $(BUILD_DIR)/$(1)-$(2)/CMakeCache.txt ] || ! grep -Fq '$(CURDIR)/$(BIN_DIR)/$(2)/$(1)' $(BUILD_DIR)/$(1)-$(2)/CMakeCache.txt; then \
		LD_LIBRARY_PATH="$(OSXCROSS_ROOT)/lib:$${LD_LIBRARY_PATH:-}" \
		PATH="$(OSXCROSS_ROOT)/bin:$$PATH" \
		$(CMAKE) -S . -B $(BUILD_DIR)/$(1)-$(2) \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=iOS \
			-DCMAKE_SYSTEM_VERSION=$(IOS_DEPLOYMENT_TARGET) \
			-DCMAKE_OSX_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
			-DCMAKE_OSX_SYSROOT=$(5) \
			-DCMAKE_OSX_ARCHITECTURES=$(6) \
			-DCMAKE_SYSTEM_PROCESSOR=$(1) \
			-DCMAKE_C_COMPILER=$(3) \
			-DCMAKE_AR=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ar \
			-DCMAKE_RANLIB=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ranlib \
			-DTLS_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-$(2)/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(2)/$(1) \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(2)/$(1) \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-$(2),LD_LIBRARY_PATH="$(OSXCROSS_ROOT)/lib:$${LD_LIBRARY_PATH:-}" PATH="$(OSXCROSS_ROOT)/bin:$$PATH" $(CMAKE) -S . -B $(BUILD_DIR)/$(1)-$(2) -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_SYSTEM_VERSION=$(IOS_DEPLOYMENT_TARGET) -DCMAKE_OSX_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) -DCMAKE_OSX_SYSROOT=$(5) -DCMAKE_OSX_ARCHITECTURES=$(6) -DCMAKE_SYSTEM_PROCESSOR=$(1) -DCMAKE_C_COMPILER=$(3) -DCMAKE_AR=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ar -DCMAKE_RANLIB=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ranlib -DTLS_BUILD_VERSION=$$ver -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-$(2)/out -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(2)/$(1) -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(2)/$(1) -G Ninja -Wno-dev > /dev/null,LD_LIBRARY_PATH="$(OSXCROSS_ROOT)/lib:$${LD_LIBRARY_PATH:-}" PATH="$(OSXCROSS_ROOT)/bin:$$PATH")
	@echo "OK $(2)/$(1)"
endef

ios/aarch64:
	$(call ios_target,aarch64,ios,$(OSXCROSS_IOS_AARCH64_CC),IPHONEOS_SDK,$(IPHONEOS_SDK),arm64)

iossim/aarch64:
	$(call ios_target,aarch64,iossim,$(OSXCROSS_IOSSIM_AARCH64_CC),IPHONESIMULATOR_SDK,$(IPHONESIMULATOR_SDK),arm64)

iossim/x86_64:
	$(call ios_target,x86_64,iossim,$(OSXCROSS_IOSSIM_X86_64_CC),IPHONESIMULATOR_SDK,$(IPHONESIMULATOR_SDK),x86_64)

define android_target
	@mkdir -p $(BIN_DIR)/android/$(1)
	@if [ ! -f $(BUILD_DIR)/$(1)-android/CMakeCache.txt ] || ! grep -Fq '$(CURDIR)/$(BIN_DIR)/android/$(1)' $(BUILD_DIR)/$(1)-android/CMakeCache.txt; then \
		$(CMAKE) -S . -B $(BUILD_DIR)/$(1)-android \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_TOOLCHAIN_FILE=$(NDK_TOOLCHAIN) \
			-DANDROID_ABI=$(2) \
			-DANDROID_PLATFORM=android-21 \
			-DTLS_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-android/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/android/$(1) \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/android/$(1) \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-android,$(CMAKE) -S . -B $(BUILD_DIR)/$(1)-android -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$(NDK_TOOLCHAIN) -DANDROID_ABI=$(2) -DANDROID_PLATFORM=android-21 -DTLS_BUILD_VERSION=$$ver -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-android/out -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/android/$(1) -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/android/$(1) -G Ninja -Wno-dev > /dev/null)
	@echo "OK android/$(1)"
endef

android/aarch64:
	$(call android_target,aarch64,arm64-v8a)

android/armv7:
	$(call android_target,armv7,armeabi-v7a)

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
		if [ ! -f $(BIN_DIR)/windows/x86_64/libtls.dll ] || [ ! -f $(BIN_DIR)/windows/x86_64/tls.exe ]; then \
			echo "Missing Windows artifacts. Run 'make windows/x86_64' or 'make all' first." >&2; \
			exit 1; \
		fi; \
		if [ ! -f $(BUILD_DIR)/test-wine/CMakeCache.txt ] || ! grep -Fq '$(CURDIR)/$(BIN_DIR)/windows/x86_64' $(BUILD_DIR)/test-wine/CMakeCache.txt; then \
			$(CMAKE) -S . -B $(BUILD_DIR)/test-wine \
				-DCMAKE_BUILD_TYPE=Release \
				-DCMAKE_SYSTEM_NAME=Windows \
				-DCMAKE_SYSTEM_PROCESSOR=x86_64 \
				-DCMAKE_C_COMPILER=$(WINE_X86_64_CC) \
				-DTLS_BUILD_TESTS=ON \
				-DTLS_BUILD_VERSION=$(BUILD_VERSION) \
				-G Ninja -Wno-dev > /dev/null; \
		fi; \
		$(CMAKE) -S . -B $(BUILD_DIR)/test-wine \
			-DTLS_CLI_PATH=$(CURDIR)/$(BIN_DIR)/windows/x86_64/tls.exe > /dev/null; \
		$(CMAKE) --build $(BUILD_DIR)/test-wine --target tls_contract_test; \
		WINEDEBUG=-all $(WINE) \
			$(BUILD_DIR)/test-wine/tls_contract_test.exe all; \
	else \
		if [ ! -f $(BIN_DIR)/$(NATIVE_TARGET)/$(NATIVE_SHARED_NAME) ] || [ ! -f $(BIN_DIR)/$(NATIVE_TARGET)/$(NATIVE_CLI_NAME) ]; then \
			echo "Missing native artifacts. Run 'make' first." >&2; \
			exit 1; \
		fi; \
		if [ ! -f $(BUILD_DIR)/test/CMakeCache.txt ] || ! grep -Fq '$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)' $(BUILD_DIR)/test/CMakeCache.txt; then \
			$(CMAKE) -S . -B $(BUILD_DIR)/test \
				-DCMAKE_BUILD_TYPE=Release \
				-DTLS_BUILD_TESTS=ON \
				-DTLS_BUILD_VERSION=$(BUILD_VERSION) \
				-G Ninja -Wno-dev > /dev/null; \
		fi; \
		$(CMAKE) -S . -B $(BUILD_DIR)/test \
			-DTLS_CLI_PATH=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/$(NATIVE_CLI_NAME) > /dev/null; \
		$(CMAKE) --build $(BUILD_DIR)/test --target tls_contract_test; \
		$(BUILD_DIR)/test/tls_contract_test all; \
	fi

wine:
	@if [ -z "$(filter test,$(MAKECMDGOALS))" ]; then \
		echo "Use 'make test wine' to run tests through Wine." >&2; \
		exit 1; \
	fi
	@:

clean:
	@rm -rf $(BUILD_DIR) $(BIN_DIR)
	@echo "OK clean"
