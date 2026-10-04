## Makefile
## Summary: Cross-compilation builder for rtc.
##
## Author:  KaisarCode
## Website: https://kaisarcode.com
## License: https://www.gnu.org/licenses/gpl-3.0.html

ifndef LIBDATACHANNEL_DIR
$(error LIBDATACHANNEL_DIR is required and must point to libdatachannel sources)
endif

ifeq ($(wildcard $(LIBDATACHANNEL_DIR)/CMakeLists.txt),)
$(error Invalid LIBDATACHANNEL_DIR: $(LIBDATACHANNEL_DIR))
endif

BUILD_DIR := .build
BIN_DIR   := bin
BUILD_VERSION ?= $(shell date +%s)
XDG_DATA_HOME ?= $(HOME)/.local/share
OSXCROSS_ROOT ?= $(XDG_DATA_HOME)/osxcross/target
MACOSX_DEPLOYMENT_TARGET ?= 11.0
DETECTED_MACOSX_SDK := $(shell ls -d "$(OSXCROSS_ROOT)"/SDK/MacOSX*.sdk 2>/dev/null | sort -V | tail -n 1)
MACOSX_SDK ?= $(DETECTED_MACOSX_SDK)
OSXCROSS_X86_64_CC := $(OSXCROSS_ROOT)/bin/o64-clang
OSXCROSS_X86_64_CXX := $(OSXCROSS_ROOT)/bin/o64-clang++
OSXCROSS_AARCH64_CC := $(OSXCROSS_ROOT)/bin/oa64-clang
OSXCROSS_AARCH64_CXX := $(OSXCROSS_ROOT)/bin/oa64-clang++
WINE ?= wine
WINE_X86_64_CC ?= x86_64-w64-mingw32-gcc
WINE_X86_64_CXX ?= x86_64-w64-mingw32-g++

HOST_ARCH       := $(shell uname -m)
HOST_SYSTEM     := $(shell uname -s)
NATIVE_ARCH     := unsupported
NATIVE_PLATFORM := unsupported

ifneq ($(filter x86_64 amd64,$(HOST_ARCH)),)
NATIVE_ARCH := x86_64
endif
ifneq ($(filter aarch64 arm64,$(HOST_ARCH)),)
NATIVE_ARCH := aarch64
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
NATIVE_SHARED_LIBRARY := librtc.so
NATIVE_IMPORT_ARGUMENT :=

ifeq ($(NATIVE_PLATFORM),windows)
NATIVE_SHARED_LIBRARY := librtc.dll
NATIVE_IMPORT_ARGUMENT := -DRTC_TEST_IMPORT_LIBRARY=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/librtc.dll.a
endif
ifeq ($(NATIVE_PLATFORM),macos)
NATIVE_SHARED_LIBRARY := librtc.dylib
endif

.DEFAULT_GOAL := $(NATIVE_TARGET)

.PHONY: all test wine clean \
	x86_64/linux aarch64/linux \
	x86_64/windows \
	x86_64/macos aarch64/macos

all: x86_64/linux aarch64/linux x86_64/windows x86_64/macos aarch64/macos

## Linux
## $(1)=arch $(2)=processor $(3)=CC $(4)=CXX

define linux_target
	@mkdir -p $(BIN_DIR)/$(1)/linux
	@cmake -S . -B $(BUILD_DIR)/$(1)-linux \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_SYSTEM_NAME=Linux \
		-DCMAKE_SYSTEM_PROCESSOR=$(2) \
		-DRTC_BUILD_VERSION=$(BUILD_VERSION) \
		-DLIBDATACHANNEL_DIR=$(LIBDATACHANNEL_DIR) \
		-DCMAKE_C_COMPILER=$(3) \
		-DCMAKE_CXX_COMPILER=$(4) \
		-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-linux/out \
		-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux \
		-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux \
		-G Ninja -Wno-dev > /dev/null
	@cmake --build $(BUILD_DIR)/$(1)-linux
	@echo "OK $(1)/linux"
endef

x86_64/linux:
	$(call linux_target,x86_64,x86_64,gcc,g++)

aarch64/linux:
	$(call linux_target,aarch64,aarch64,aarch64-linux-gnu-gcc,aarch64-linux-gnu-g++)

## Windows

x86_64/windows:
	@mkdir -p $(BIN_DIR)/x86_64/windows
	@cmake -S . -B $(BUILD_DIR)/x86_64-windows \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_SYSTEM_NAME=Windows \
		-DRTC_BUILD_VERSION=$(BUILD_VERSION) \
		-DLIBDATACHANNEL_DIR=$(LIBDATACHANNEL_DIR) \
		-DCMAKE_C_COMPILER=$(WINE_X86_64_CC) \
		-DCMAKE_CXX_COMPILER=$(WINE_X86_64_CXX) \
		-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/x86_64-windows/out \
		-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/x86_64/windows \
		-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/x86_64/windows \
		-G Ninja -Wno-dev > /dev/null
	@cmake --build $(BUILD_DIR)/x86_64-windows
	@cp $(BUILD_DIR)/x86_64-windows/out/librtc.dll $(BIN_DIR)/x86_64/windows/librtc.dll
	@echo "OK x86_64/windows"

## macOS
## $(1)=arch $(2)=CC $(3)=CXX

define macos_target
	@mkdir -p $(BIN_DIR)/$(1)/macos
	@if ! $(2) --version > /dev/null 2>&1 || ! $(3) --version > /dev/null 2>&1; then \
		echo "Missing macOS cross compiler for $(1)" >&2; \
		exit 1; \
	fi
	@LD_LIBRARY_PATH="$(OSXCROSS_ROOT)/lib:$${LD_LIBRARY_PATH:-}" \
	PATH="$(OSXCROSS_ROOT)/bin:$$PATH" \
	cmake -S . -B $(BUILD_DIR)/$(1)-macos \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_SYSTEM_NAME=Darwin \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=$(MACOSX_DEPLOYMENT_TARGET) \
		-DRTC_BUILD_VERSION=$(BUILD_VERSION) \
		-DLIBDATACHANNEL_DIR=$(LIBDATACHANNEL_DIR) \
		-DCMAKE_C_COMPILER=$(2) \
		-DCMAKE_CXX_COMPILER=$(3) \
		-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-macos/out \
		-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/macos \
		-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/macos \
		-G Ninja -Wno-dev > /dev/null
	@LD_LIBRARY_PATH="$(OSXCROSS_ROOT)/lib:$${LD_LIBRARY_PATH:-}" \
	PATH="$(OSXCROSS_ROOT)/bin:$$PATH" \
	cmake --build $(BUILD_DIR)/$(1)-macos
	@echo "OK $(1)/macos"
endef

x86_64/macos:
	$(call macos_target,x86_64,$(OSXCROSS_X86_64_CC),$(OSXCROSS_X86_64_CXX))

aarch64/macos:
	$(call macos_target,aarch64,$(OSXCROSS_AARCH64_CC),$(OSXCROSS_AARCH64_CXX))

## Utility

test:
	@if [ -n "$(filter wine,$(MAKECMDGOALS))" ]; then \
		if ! command -v $(WINE) >/dev/null 2>&1; then \
			echo "Missing Wine runtime: $(WINE)" >&2; \
			exit 1; \
		fi; \
		if [ ! -f $(BIN_DIR)/x86_64/windows/librtc.dll ] || \
		   [ ! -f $(BIN_DIR)/x86_64/windows/librtc.dll.a ]; then \
			echo "Missing Windows artifacts. Run 'make x86_64/windows' first." >&2; \
			exit 1; \
		fi; \
		cmake -S . -B $(BUILD_DIR)/test-wine \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Windows \
			-DRTC_BUILD_TESTS=ON \
			-DRTC_BUILD_VERSION=$(BUILD_VERSION) \
			-DLIBDATACHANNEL_DIR=$(LIBDATACHANNEL_DIR) \
			-DRTC_TEST_SHARED_LIBRARY=$(CURDIR)/$(BIN_DIR)/x86_64/windows/librtc.dll \
			-DRTC_TEST_IMPORT_LIBRARY=$(CURDIR)/$(BIN_DIR)/x86_64/windows/librtc.dll.a \
			-DCMAKE_C_COMPILER=$(WINE_X86_64_CC) \
			-DCMAKE_CXX_COMPILER=$(WINE_X86_64_CXX) \
			-DCMAKE_CROSSCOMPILING_EMULATOR=$(WINE) \
			-G Ninja -Wno-dev > /dev/null; \
		cmake --build $(BUILD_DIR)/test-wine --target rtc_contract_test; \
		WINEDEBUG=-all $(WINE) $(BUILD_DIR)/test-wine/rtc_contract_test.exe all; \
	else \
		if [ ! -f $(BIN_DIR)/$(NATIVE_TARGET)/$(NATIVE_SHARED_LIBRARY) ]; then \
			echo "Missing native artifacts. Run 'make' first." >&2; \
			exit 1; \
		fi; \
		cmake -S . -B $(BUILD_DIR)/test \
			-DCMAKE_BUILD_TYPE=Release \
			-DRTC_BUILD_TESTS=ON \
			-DRTC_BUILD_VERSION=$(BUILD_VERSION) \
			-DLIBDATACHANNEL_DIR=$(LIBDATACHANNEL_DIR) \
			-DRTC_TEST_SHARED_LIBRARY=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/$(NATIVE_SHARED_LIBRARY) \
			$(NATIVE_IMPORT_ARGUMENT) \
			-G Ninja -Wno-dev > /dev/null; \
		cmake --build $(BUILD_DIR)/test --target rtc_contract_test; \
		$(BUILD_DIR)/test/rtc_contract_test all; \
	fi

wine:
	@if [ -z "$(filter test,$(MAKECMDGOALS))" ]; then \
		echo "Use 'make test wine' to run tests through Wine." >&2; \
		exit 1; \
	fi
	@:

clean:
	@rm -rf $(BUILD_DIR)
	@echo "OK clean"
