## Makefile
## Summary: Cross-compilation builder for ppn.
##
## Author:  KaisarCode
## Website: https://kaisarcode.com
## License: https://www.gnu.org/licenses/gpl-3.0.html

BUILD_DIR := .build
BIN_DIR   := bin
BUILD_VERSION ?= $(shell date +%s)
XDG_DATA_HOME ?= $(HOME)/.local/share
OSXCROSS_ROOT ?= $(XDG_DATA_HOME)/osxcross/target
MACOSX_DEPLOYMENT_TARGET ?= 11.0
DETECTED_MACOSX_SDK := $(shell ls -d "$(OSXCROSS_ROOT)"/SDK/MacOSX*.sdk 2>/dev/null | sort -V | tail -n 1)
MACOSX_SDK ?= $(DETECTED_MACOSX_SDK)
WINE_X86_64_CC ?= x86_64-w64-mingw32-gcc

HOST_ARCH       := $(shell uname -m)
HOST_SYSTEM     := $(shell uname -s)
NATIVE_ARCH     := unsupported
NATIVE_PLATFORM := unsupported

ifneq ($(filter x86_64 amd64,$(HOST_ARCH)),)
NATIVE_ARCH := x86_64
endif
ifeq ($(HOST_SYSTEM),Linux)
NATIVE_PLATFORM := linux
endif

NATIVE_TARGET := $(NATIVE_ARCH)/$(NATIVE_PLATFORM)
NATIVE_SHARED_LIBRARY := libppn.so

.DEFAULT_GOAL := $(NATIVE_TARGET)

.PHONY: all test wine clean x86_64/linux x86_64/windows \
	x86_64/macos aarch64/macos

all: x86_64/linux x86_64/windows x86_64/macos aarch64/macos

## Linux

x86_64/linux:
	@mkdir -p $(BIN_DIR)/x86_64/linux
	@if [ ! -f $(BUILD_DIR)/x86_64-linux/CMakeCache.txt ]; then \
		cmake -S . -B $(BUILD_DIR)/x86_64-linux \
			-DCMAKE_BUILD_TYPE=Release \
			-DPPN_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_C_COMPILER=gcc \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/x86_64/linux \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/x86_64/linux \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	@cmake --build $(BUILD_DIR)/x86_64-linux
	@cd $(BIN_DIR)/x86_64/linux && sha256sum libppn.a libppn.so > SHA256SUMS
	@echo "OK x86_64/linux"

## Windows

x86_64/windows:
	@mkdir -p $(BIN_DIR)/x86_64/windows
	@if [ ! -f $(BUILD_DIR)/x86_64-windows/CMakeCache.txt ]; then \
		cmake -S . -B $(BUILD_DIR)/x86_64-windows \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Windows \
			-DPPN_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_C_COMPILER=$(WINE_X86_64_CC) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/x86_64-windows/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/x86_64/windows \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/x86_64/windows \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	@cmake --build $(BUILD_DIR)/x86_64-windows
	@cp $(BUILD_DIR)/x86_64-windows/out/libppn.dll $(BIN_DIR)/x86_64/windows/libppn.dll
	@cd $(BIN_DIR)/x86_64/windows && sha256sum libppn.a libppn.dll libppn.dll.a > SHA256SUMS
	@echo "OK x86_64/windows"

## macOS

define macos_target
	@mkdir -p $(BIN_DIR)/$(1)/macos
	@if ! $(2) --version > /dev/null 2>&1; then \
		echo "Missing macOS cross-compiler wrapper: $(2)" >&2; \
		exit 1; \
	fi
	@export OSXCROSS_HOST=$(1)-apple-darwin25.1 && \
	export OSXCROSS_TARGET_DIR=$(OSXCROSS_ROOT) && \
	export OSXCROSS_TARGET=darwin25.1 && \
	export OSXCROSS_SDK=$(MACOSX_SDK) && \
	export LD_LIBRARY_PATH="$(OSXCROSS_ROOT)/lib:$${LD_LIBRARY_PATH:-}" && \
	export PATH="$(OSXCROSS_ROOT)/bin:$$PATH" && \
	if [ ! -f $(BUILD_DIR)/$(1)-macos/build.ninja ]; then \
		cmake -S . -B $(BUILD_DIR)/$(1)-macos \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Darwin \
			-DCMAKE_OSX_DEPLOYMENT_TARGET=$(MACOSX_DEPLOYMENT_TARGET) \
			-DPPN_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_C_COMPILER=$(2) \
			-DCMAKE_OBJC_COMPILER=$(2) \
			-DCMAKE_INSTALL_NAME_TOOL=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-install_name_tool \
			-DCMAKE_AR=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ar \
			-DCMAKE_RANLIB=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ranlib \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/macos \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/macos \
			-G Ninja -Wno-dev > /dev/null; \
	fi && \
	cmake --build $(BUILD_DIR)/$(1)-macos && \
	cd $(BIN_DIR)/$(1)/macos && \
	shasum -a 256 libppn.a libppn.dylib > SHA256SUMS && \
	echo "OK $(1)/macos"
endef

x86_64/macos:
	$(call macos_target,x86_64,$(OSXCROSS_ROOT)/bin/o64-clang)

aarch64/macos:
	$(call macos_target,aarch64,$(OSXCROSS_ROOT)/bin/oa64-clang)

## Test

test:
	@if [ -n "$(filter wine,$(MAKECMDGOALS))" ]; then \
		if [ ! -f $(BUILD_DIR)/test-wine/CMakeCache.txt ]; then \
			cmake -S . -B $(BUILD_DIR)/test-wine \
				-DCMAKE_BUILD_TYPE=Release \
				-DCMAKE_SYSTEM_NAME=Windows \
				-DCMAKE_C_COMPILER=$(WINE_X86_64_CC) \
				-DPPN_BUILD_TESTS=ON \
				-DPPN_BUILD_VERSION=$(BUILD_VERSION) \
				-DPPN_TEST_SHARED_LIBRARY=$(CURDIR)/$(BIN_DIR)/x86_64/windows/libppn.dll \
				-DPPN_TEST_IMPORT_LIBRARY=$(CURDIR)/$(BIN_DIR)/x86_64/windows/libppn.dll.a \
				-DCMAKE_CROSSCOMPILING_EMULATOR=wine \
				-G Ninja -Wno-dev > /dev/null; \
		fi; \
		cmake --build $(BUILD_DIR)/test-wine --target ppn_contract_test || exit 1; \
		WINEDEBUG=-all wine $(BUILD_DIR)/test-wine/ppn_contract_test.exe 2>/dev/null; \
	else \
		if [ ! -f $(BUILD_DIR)/test/CMakeCache.txt ]; then \
			cmake -S . -B $(BUILD_DIR)/test \
				-DCMAKE_BUILD_TYPE=Release \
				-DPPN_BUILD_TESTS=ON \
				-DPPN_BUILD_VERSION=$(BUILD_VERSION) \
				-DPPN_TEST_SHARED_LIBRARY=$(CURDIR)/$(BIN_DIR)/$(NATIVE_ARCH)/$(NATIVE_PLATFORM)/$(NATIVE_SHARED_LIBRARY) \
				-G Ninja -Wno-dev > /dev/null; \
		fi; \
		cmake --build $(BUILD_DIR)/test --target ppn_contract_test || exit 1; \
		$(BUILD_DIR)/test/ppn_contract_test; \
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
