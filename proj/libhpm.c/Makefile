## Makefile
## Summary: Native builder for hpm.
##
## Author:  KaisarCode
## Website: https://kaisarcode.com
## License: https://www.gnu.org/licenses/lgpl-3.0.html

SHELL := /bin/sh

BUILD_DIR := .build
BIN_DIR := bin
BUILD_VERSION ?= $(shell date +%s)

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

.DEFAULT_GOAL := $(NATIVE_TARGET)

.PHONY: all test clean $(NATIVE_TARGET)

all: $(NATIVE_TARGET)

$(NATIVE_TARGET):
	@if [ "$(NATIVE_ARCH)" = "unsupported" ] || [ "$(NATIVE_PLATFORM)" = "unsupported" ]; then \
		echo "kclib: unsupported native target $(HOST_ARCH)/$(HOST_SYSTEM)" >&2; exit 1; \
	fi
	@mkdir -p '$(BIN_DIR)/$(NATIVE_TARGET)'
	@cmake -S . -B '$(BUILD_DIR)/native' \
		-DCMAKE_BUILD_TYPE=Release \
		-DHPM_BUILD_VERSION=$(BUILD_VERSION) \
		-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY='$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)' \
		-DCMAKE_LIBRARY_OUTPUT_DIRECTORY='$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)' \
		-DCMAKE_RUNTIME_OUTPUT_DIRECTORY='$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)' \
		-G Ninja -Wno-dev > /dev/null
	@cmake --build '$(BUILD_DIR)/native'
	@cp src/libhpm.h '$(BIN_DIR)/$(NATIVE_TARGET)/libhpm.h'
	@echo "OK $(NATIVE_TARGET)"

test:
	@if [ "$(NATIVE_ARCH)" = "unsupported" ] || [ "$(NATIVE_PLATFORM)" = "unsupported" ]; then \
		echo "kclib: unsupported native target $(HOST_ARCH)/$(HOST_SYSTEM)" >&2; exit 1; \
	fi
	@cmake -S . -B '$(BUILD_DIR)/test' \
		-DCMAKE_BUILD_TYPE=Release \
		-DHPM_BUILD_VERSION=$(BUILD_VERSION) \
		-DHPM_BUILD_TESTS=ON \
		-G Ninja -Wno-dev > /dev/null
	@cmake --build '$(BUILD_DIR)/test'
	@'$(BUILD_DIR)/test/hpm_contract_test' all

clean:
	@rm -rf '$(BUILD_DIR)' '$(BIN_DIR)'
	@echo "OK clean"
