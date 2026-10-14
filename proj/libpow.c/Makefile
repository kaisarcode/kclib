## Makefile
## Summary: Cross-platform builder for pow.
##
## Author:  KaisarCode
## Website: https://kaisarcode.com
## License: https://www.gnu.org/licenses/lgpl-3.0.html

BUILD_DIR := .build
BIN_DIR   := bin
BUILD_VERSION ?= $(shell date +%s)
WINE ?= wine
WINE_X86_64_CC ?= x86_64-w64-mingw32-gcc
WASM_EMCMAKE ?= emcmake
WASM_NODE ?= node

HOST_ARCH   := $(shell uname -m)
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
ifeq ($(HOST_SYSTEM),Linux)
NATIVE_PLATFORM := linux
endif
ifneq ($(filter MINGW% MSYS% CYGWIN%,$(HOST_SYSTEM)),)
NATIVE_PLATFORM := windows
endif

NATIVE_TARGET := $(NATIVE_ARCH)/$(NATIVE_PLATFORM)
NATIVE_SHARED_NAME := libpow.so
NATIVE_IMPORT_LIBRARY :=
ifeq ($(NATIVE_PLATFORM),windows)
NATIVE_SHARED_NAME := libpow.dll
NATIVE_IMPORT_LIBRARY := -DPOW_TEST_IMPORT_LIBRARY=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/libpow.dll.a
endif

.DEFAULT_GOAL := $(NATIVE_TARGET)

.PHONY: test wine wasm clean x86_64/linux i686/linux aarch64/linux armv7/linux \
	x86_64/windows i686/windows wasm32/wasm

x86_64/linux:
	@mkdir -p $(BIN_DIR)/x86_64/linux
	@cmake -S . -B $(BUILD_DIR)/x86_64-linux -DCMAKE_BUILD_TYPE=Release \
		-DPOW_BUILD_VERSION=$(BUILD_VERSION) -DCMAKE_SYSTEM_NAME=Linux \
		-DCMAKE_C_COMPILER=gcc \
		-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/x86_64/linux \
		-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/x86_64/linux \
		-G Ninja -Wno-dev >/dev/null
	@cmake --build $(BUILD_DIR)/x86_64-linux
	@echo "OK x86_64/linux"

i686/linux:
	@mkdir -p $(BIN_DIR)/i686/linux
	@cmake -S . -B $(BUILD_DIR)/i686-linux -DCMAKE_BUILD_TYPE=Release \
		-DPOW_BUILD_VERSION=$(BUILD_VERSION) -DCMAKE_SYSTEM_NAME=Linux \
		-DCMAKE_C_COMPILER=i686-linux-gnu-gcc \
		-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/i686/linux \
		-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/i686/linux \
		-G Ninja -Wno-dev >/dev/null
	@cmake --build $(BUILD_DIR)/i686-linux
	@echo "OK i686/linux"

aarch64/linux:
	@mkdir -p $(BIN_DIR)/aarch64/linux
	@cmake -S . -B $(BUILD_DIR)/aarch64-linux -DCMAKE_BUILD_TYPE=Release \
		-DPOW_BUILD_VERSION=$(BUILD_VERSION) -DCMAKE_SYSTEM_NAME=Linux \
		-DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
		-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/aarch64/linux \
		-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/aarch64/linux \
		-G Ninja -Wno-dev >/dev/null
	@cmake --build $(BUILD_DIR)/aarch64-linux
	@echo "OK aarch64/linux"

armv7/linux:
	@mkdir -p $(BIN_DIR)/armv7/linux
	@cmake -S . -B $(BUILD_DIR)/armv7-linux -DCMAKE_BUILD_TYPE=Release \
		-DPOW_BUILD_VERSION=$(BUILD_VERSION) -DCMAKE_SYSTEM_NAME=Linux \
		-DCMAKE_C_COMPILER=arm-linux-gnueabi-gcc \
		-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/armv7/linux \
		-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/armv7/linux \
		-G Ninja -Wno-dev >/dev/null
	@cmake --build $(BUILD_DIR)/armv7-linux
	@echo "OK armv7/linux"

x86_64/windows:
	@mkdir -p $(BIN_DIR)/x86_64/windows
	@cmake -S . -B $(BUILD_DIR)/x86_64-windows -DCMAKE_BUILD_TYPE=Release \
		-DPOW_BUILD_VERSION=$(BUILD_VERSION) -DCMAKE_SYSTEM_NAME=Windows \
		-DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
		-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/x86_64/windows \
		-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/x86_64/windows \
		-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/x86_64/windows \
		-G Ninja -Wno-dev >/dev/null
	@cmake --build $(BUILD_DIR)/x86_64-windows
	@echo "OK x86_64/windows"

i686/windows:
	@mkdir -p $(BIN_DIR)/i686/windows
	@cmake -S . -B $(BUILD_DIR)/i686-windows -DCMAKE_BUILD_TYPE=Release \
		-DPOW_BUILD_VERSION=$(BUILD_VERSION) -DCMAKE_SYSTEM_NAME=Windows \
		-DCMAKE_C_COMPILER=i686-w64-mingw32-gcc \
		-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/i686/windows \
		-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/i686/windows \
		-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/i686/windows \
		-G Ninja -Wno-dev >/dev/null
	@cmake --build $(BUILD_DIR)/i686-windows
	@echo "OK i686/windows"

wasm32/wasm:
	@mkdir -p $(BIN_DIR)/wasm32/wasm
	@$(WASM_EMCMAKE) cmake -S . -B $(BUILD_DIR)/wasm32-wasm -DCMAKE_BUILD_TYPE=Release \
		-DPOW_BUILD_VERSION=$(BUILD_VERSION) \
		-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/wasm32-wasm/out \
		-G Ninja -Wno-dev >/dev/null
	@cmake --build $(BUILD_DIR)/wasm32-wasm
	@cp $(BUILD_DIR)/wasm32-wasm/out/libpow.wasm $(BIN_DIR)/wasm32/wasm/libpow.wasm
	@echo "OK wasm32/wasm"

test:
	@if [ "$(NATIVE_ARCH)" = "unsupported" ] || [ "$(NATIVE_PLATFORM)" = "unsupported" ]; then \
		echo "Unsupported native test target $(HOST_ARCH)/$(HOST_SYSTEM)" >&2; exit 1; fi
	@if [ ! -f $(BIN_DIR)/$(NATIVE_TARGET)/$(NATIVE_SHARED_NAME) ]; then \
		echo "Missing native artifacts. Run 'make' first." >&2; exit 1; fi
	@cmake -S . -B $(BUILD_DIR)/test -DCMAKE_BUILD_TYPE=Release \
		-DPOW_BUILD_TESTS=ON -DPOW_BUILD_VERSION=$(BUILD_VERSION) \
		-DPOW_TEST_SHARED_LIBRARY=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/$(NATIVE_SHARED_NAME) \
		$(NATIVE_IMPORT_LIBRARY) -G Ninja -Wno-dev >/dev/null
	@cmake --build $(BUILD_DIR)/test --target pow_contract_test
	@$(BUILD_DIR)/test/pow_contract_test all

wine:
	@echo "Build x86_64/windows and configure POW_TEST_SHARED_LIBRARY to run contract tests through Wine."

wasm:
	@$(WASM_EMCMAKE) cmake -S . -B $(BUILD_DIR)/test-wasm -DCMAKE_BUILD_TYPE=Release \
		-DPOW_BUILD_TESTS=ON -DPOW_BUILD_VERSION=$(BUILD_VERSION) -G Ninja -Wno-dev >/dev/null
	@cmake --build $(BUILD_DIR)/test-wasm --target pow_contract_test
	@$(WASM_NODE) $(BUILD_DIR)/test-wasm/pow_contract_test.js all

clean:
	@rm -rf $(BUILD_DIR)
	@echo "OK clean"
