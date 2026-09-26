## Makefile
## Summary: Cross-compilation builder for tray.
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
WINE_X86_64_CC ?= x86_64-w64-mingw32-gcc

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

ifeq ($(HOST_SYSTEM),Linux)
NATIVE_PLATFORM := linux
endif

NATIVE_TARGET := $(NATIVE_ARCH)/$(NATIVE_PLATFORM)
NATIVE_SHARED_LIBRARY := libtray.so
NATIVE_IMPORT_ARGUMENT :=

.DEFAULT_GOAL := $(NATIVE_TARGET)

.PHONY: all test wine clean \
	x86_64/linux x86_64/windows x86_64/macos \
	aarch64/macos

all: x86_64/linux x86_64/windows x86_64/macos aarch64/macos

## Linux

define linux_target
	@mkdir -p $(BIN_DIR)/$(1)/linux
	@if [ ! -f $(BUILD_DIR)/$(subst /,-,$(1))-linux/CMakeCache.txt ]; then \
		cmake -S . -B $(BUILD_DIR)/$(subst /,-,$(1))-linux \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Linux \
			-DTRAY_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_C_COMPILER=$(2) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(subst /,-,$(1))-linux/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(subst /,-,$(1))-linux,cmake -S . -B $(BUILD_DIR)/$(subst /,-,$(1))-linux -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Linux -DTRAY_BUILD_VERSION=$$ver -DCMAKE_C_COMPILER=$(2) -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(subst /,-,$(1))-linux/out -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux -G Ninja -Wno-dev > /dev/null)
	@cp $(BUILD_DIR)/$(subst /,-,$(1))-linux/out/tray $(BIN_DIR)/$(1)/linux/tray
	@echo "OK $(1)/linux"
endef

x86_64/linux:
	$(call linux_target,x86_64,gcc)

## Windows

define windows_target
	@mkdir -p $(BIN_DIR)/$(1)/windows
	@if [ ! -f $(BUILD_DIR)/$(1)-windows/CMakeCache.txt ]; then \
		cmake -S . -B $(BUILD_DIR)/$(1)-windows \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Windows \
			-DTRAY_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_C_COMPILER=$(2) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-windows/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-windows,cmake -S . -B $(BUILD_DIR)/$(1)-windows -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Windows -DTRAY_BUILD_VERSION=$$ver -DCMAKE_C_COMPILER=$(2) -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-windows/out -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows -G Ninja -Wno-dev > /dev/null)
	@cp $(BUILD_DIR)/$(1)-windows/out/tray.exe $(BIN_DIR)/$(1)/windows/tray.exe
	@cp $(BUILD_DIR)/$(1)-windows/out/libtray.dll $(BIN_DIR)/$(1)/windows/libtray.dll
	@echo "OK $(1)/windows"
endef

x86_64/windows:
	$(call windows_target,x86_64,$(WINE_X86_64_CC))

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
	if [ -f "$$cache" ] && ( ! grep -q '^CMAKE_C_COMPILER:.*=$(2)$$' "$$cache" || ! grep -q '^CMAKE_OBJC_COMPILER:.*=$(2)$$' "$$cache" || grep -q '^CMAKE_INSTALL_NAME_TOOL:.*NOTFOUND' "$$cache" ); then \
		rm -f "$$cache" $(BUILD_DIR)/$(1)-macos/build.ninja && rm -rf $(BUILD_DIR)/$(1)-macos/CMakeFiles; \
	fi && \
	if [ ! -f $(BUILD_DIR)/$(1)-macos/build.ninja ]; then \
		cmake -S . -B $(BUILD_DIR)/$(1)-macos \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Darwin \
			-DCMAKE_OSX_DEPLOYMENT_TARGET=$(MACOSX_DEPLOYMENT_TARGET) \
			-DTRAY_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_C_COMPILER=$(2) \
			-DCMAKE_OBJC_COMPILER=$(2) \
			-DCMAKE_INSTALL_NAME_TOOL=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-install_name_tool \
			-DCMAKE_AR=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ar \
			-DCMAKE_RANLIB=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ranlib \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-macos/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/macos \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/macos \
			-G Ninja -Wno-dev > /dev/null; \
	fi && \
	cmake --build $(BUILD_DIR)/$(1)-macos 2>&1 && \
	cp $(BUILD_DIR)/$(1)-macos/out/tray $(BIN_DIR)/$(1)/macos/tray && \
	echo "OK $(1)/macos"
endef

x86_64/macos:
	$(call macos_target,x86_64,$(OSXCROSS_ROOT)/bin/o64-clang)

aarch64/macos:
	$(call macos_target,aarch64,$(OSXCROSS_ROOT)/bin/oa64-clang)

## Test

test:
	@if [ -n "$(filter wine,$(MAKECMDGOALS))" ]; then \
		if ! command -v wine >/dev/null 2>&1; then \
			echo "Missing Wine runtime: wine" >&2; \
			exit 1; \
		fi; \
		if ! command -v $(WINE_X86_64_CC) >/dev/null 2>&1; then \
			echo "Missing Windows cross-compiler: $(WINE_X86_64_CC)" >&2; \
			exit 1; \
		fi; \
		if [ ! -f $(BUILD_DIR)/test-wine/CMakeCache.txt ]; then \
			cmake -S . -B $(BUILD_DIR)/test-wine \
				-DCMAKE_BUILD_TYPE=Release \
				-DCMAKE_SYSTEM_NAME=Windows \
				-DCMAKE_C_COMPILER=$(WINE_X86_64_CC) \
				-DTRAY_BUILD_VERSION=$(BUILD_VERSION) \
				-DTRAY_BUILD_TESTS=ON \
				-DTRAY_TEST_SHARED_LIBRARY=$(CURDIR)/$(BIN_DIR)/x86_64/windows/libtray.dll \
				-DTRAY_TEST_IMPORT_LIBRARY=$(CURDIR)/$(BIN_DIR)/x86_64/windows/libtray.dll.a \
				-DTRAY_TEST_CLI=$(CURDIR)/$(BIN_DIR)/x86_64/windows/tray.exe \
				-DCMAKE_CROSSCOMPILING_EMULATOR=wine \
				-G Ninja -Wno-dev > /dev/null; \
		fi; \
		cmake --build $(BUILD_DIR)/test-wine --target tray_contract_test || exit 1; \
		WINEDEBUG=-all wine $(BUILD_DIR)/test-wine/tray_contract_test.exe all 2>/dev/null; \
	else \
		if [ ! -f $(BUILD_DIR)/test/CMakeCache.txt ]; then \
			cmake -S . -B $(BUILD_DIR)/test \
				-DCMAKE_BUILD_TYPE=Release \
				-DTRAY_BUILD_VERSION=$(BUILD_VERSION) \
				-DTRAY_BUILD_TESTS=ON \
				-DTRAY_TEST_SHARED_LIBRARY=$(CURDIR)/$(BIN_DIR)/$(NATIVE_ARCH)/$(NATIVE_PLATFORM)/$(NATIVE_SHARED_LIBRARY) \
				-DTRAY_TEST_CLI=$(CURDIR)/$(BIN_DIR)/$(NATIVE_ARCH)/$(NATIVE_PLATFORM)/tray \
				$(NATIVE_IMPORT_ARGUMENT) \
				-G Ninja -Wno-dev > /dev/null; \
		fi; \
		cmake --build $(BUILD_DIR)/test --target tray_contract_test || exit 1; \
		$(BUILD_DIR)/test/tray_contract_test all 2>/dev/null; \
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
