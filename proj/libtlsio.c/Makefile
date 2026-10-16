## Makefile
## Summary: Native builder for tlsio.
##
## Author: KaisarCode
## Website: https://kaisarcode.com
## License: https://www.gnu.org/licenses/lgpl-3.0.html

BUILD_DIR := .build
BIN_DIR := bin
BUILD_VERSION ?= $(shell date +%s)
HOST_ARCH := $(shell uname -m)
HOST_SYSTEM := $(shell uname -s)
NATIVE_ARCH := $(HOST_ARCH)
NATIVE_PLATFORM := unsupported

ifneq ($(filter amd64,$(HOST_ARCH)),)
NATIVE_ARCH := x86_64
endif

ifneq ($(filter arm64,$(HOST_ARCH)),)
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

.DEFAULT_GOAL := $(NATIVE_TARGET)

.PHONY: all test clean $(NATIVE_TARGET)

all: $(NATIVE_TARGET)

$(NATIVE_TARGET):
	@mkdir -p $(BIN_DIR)/$(NATIVE_TARGET)
	@cmake -S . -B $(BUILD_DIR)/native \
		-DCMAKE_BUILD_TYPE=Release \
		-DTLSIO_BUILD_VERSION=$(BUILD_VERSION) \
		-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET) \
		-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET) \
		-G Ninja -Wno-dev > /dev/null
	@cmake --build $(BUILD_DIR)/native
	@echo "OK $(NATIVE_TARGET)"

test:
	@cmake -S . -B $(BUILD_DIR)/test \
		-DCMAKE_BUILD_TYPE=Release \
		-DTLSIO_BUILD_VERSION=$(BUILD_VERSION) \
		-DTLSIO_BUILD_TESTS=ON \
		-G Ninja -Wno-dev > /dev/null
	@cmake --build $(BUILD_DIR)/test
	@$(BUILD_DIR)/test/tlsio_contract_test all

clean:
	@rm -rf $(BUILD_DIR) $(BIN_DIR)
