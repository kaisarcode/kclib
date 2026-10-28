## Makefile
## Summary: Cross-platform builder and contract test runner for kc.
##
## Author: KaisarCode
## Website: https://kaisarcode.com
## License: GNU General Public License v3.0

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?=
LDFLAGS ?=
WINE ?= wine
WINE_X86_64_CC ?= x86_64-w64-mingw32-gcc
OSXCROSS_ROOT ?= $(HOME)/.local/share/osxcross/target

HOST_SYSTEM := $(shell uname -s)
HOST_MACHINE := $(shell uname -m)
NATIVE_PLATFORM := unsupported
NATIVE_ARCH := unsupported

ifeq ($(HOST_SYSTEM),Linux)
NATIVE_PLATFORM := linux
endif
ifeq ($(HOST_SYSTEM),Darwin)
NATIVE_PLATFORM := macos
endif
ifneq ($(filter MINGW% MSYS% CYGWIN%,$(HOST_SYSTEM)),)
NATIVE_PLATFORM := windows
endif
ifneq ($(filter x86_64 amd64 AMD64,$(HOST_MACHINE)),)
NATIVE_ARCH := x86_64
endif
ifneq ($(filter i386 i686,$(HOST_MACHINE)),)
NATIVE_ARCH := i686
endif
ifneq ($(filter arm64 aarch64 ARM64,$(HOST_MACHINE)),)
NATIVE_ARCH := aarch64
endif
ifneq ($(filter armv7l armv7,$(HOST_MACHINE)),)
NATIVE_ARCH := armv7
endif
ifneq ($(filter ppc64le powerpc64le,$(HOST_MACHINE)),)
NATIVE_ARCH := powerpc64le
endif
ifneq ($(filter riscv64 s390x loongarch64 mips mipsel mips64el,$(HOST_MACHINE)),)
NATIVE_ARCH := $(HOST_MACHINE)
endif

NATIVE_TARGET := $(NATIVE_PLATFORM)/$(NATIVE_ARCH)
TARGET ?= $(NATIVE_TARGET)
EXE := kc
ifeq ($(word 1,$(subst /, ,$(TARGET))),windows)
EXE := kc.exe
endif

BIN_DIR := bin
OUTPUT := $(BIN_DIR)/$(TARGET)/$(EXE)
NATIVE_EXE := kc
ifeq ($(NATIVE_PLATFORM),windows)
NATIVE_EXE := kc.exe
endif
NATIVE_OUTPUT := $(BIN_DIR)/$(NATIVE_TARGET)/$(NATIVE_EXE)
SOURCES := src/kc.c src/packages.c src/packages.h src/sha256.c src/sha256.h
BUILD_DIR := .build

.DEFAULT_GOAL := $(NATIVE_TARGET)

.PHONY: all build test wine clean \
    linux/x86_64 linux/i686 linux/aarch64 linux/armv7 linux/armv7hf \
    linux/riscv64 linux/powerpc64le linux/mips linux/mipsel \
    linux/mips64el linux/s390x linux/loongarch64 \
    windows/x86_64 windows/i686 \
    macos/x86_64 macos/aarch64

all: \
    linux/x86_64 linux/i686 linux/aarch64 linux/armv7 linux/armv7hf \
    linux/riscv64 linux/powerpc64le linux/mips linux/mipsel \
    linux/mips64el linux/s390x linux/loongarch64 \
    windows/x86_64 windows/i686 macos/x86_64 macos/aarch64

build: $(OUTPUT)

$(OUTPUT): $(SOURCES)
	@mkdir -p $(BIN_DIR)/$(TARGET)
	@$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -o $@ src/kc.c src/packages.c src/sha256.c $(LDFLAGS)

linux/x86_64:
	@$(MAKE) -s --no-print-directory TARGET=linux/x86_64 CC=gcc build
	@echo "OK linux/x86_64"
linux/i686:
	@$(MAKE) -s --no-print-directory TARGET=linux/i686 CC=i686-linux-gnu-gcc build
	@echo "OK linux/i686"
linux/aarch64:
	@$(MAKE) -s --no-print-directory TARGET=linux/aarch64 CC=aarch64-linux-gnu-gcc build
	@echo "OK linux/aarch64"
linux/armv7:
	@$(MAKE) -s --no-print-directory TARGET=linux/armv7 CC=arm-linux-gnueabi-gcc build
	@echo "OK linux/armv7"
linux/armv7hf:
	@$(MAKE) -s --no-print-directory TARGET=linux/armv7hf CC=arm-linux-gnueabihf-gcc build
	@echo "OK linux/armv7hf"
linux/riscv64:
	@$(MAKE) -s --no-print-directory TARGET=linux/riscv64 CC=riscv64-linux-gnu-gcc build
	@echo "OK linux/riscv64"
linux/powerpc64le:
	@$(MAKE) -s --no-print-directory TARGET=linux/powerpc64le CC=powerpc64le-linux-gnu-gcc build
	@echo "OK linux/powerpc64le"
linux/mips:
	@$(MAKE) -s --no-print-directory TARGET=linux/mips CC=mips-linux-gnu-gcc build
	@echo "OK linux/mips"
linux/mipsel:
	@$(MAKE) -s --no-print-directory TARGET=linux/mipsel CC=mipsel-linux-gnu-gcc build
	@echo "OK linux/mipsel"
linux/mips64el:
	@$(MAKE) -s --no-print-directory TARGET=linux/mips64el CC=mips64el-linux-gnuabi64-gcc build
	@echo "OK linux/mips64el"
linux/s390x:
	@$(MAKE) -s --no-print-directory TARGET=linux/s390x CC=s390x-linux-gnu-gcc build
	@echo "OK linux/s390x"
linux/loongarch64:
	@$(MAKE) -s --no-print-directory TARGET=linux/loongarch64 CC=loongarch64-linux-gnu-gcc build
	@echo "OK linux/loongarch64"
windows/x86_64:
	@$(MAKE) -s --no-print-directory TARGET=windows/x86_64 CC=x86_64-w64-mingw32-gcc build
	@echo "OK windows/x86_64"
windows/i686:
	@$(MAKE) -s --no-print-directory TARGET=windows/i686 CC=i686-w64-mingw32-gcc build
	@echo "OK windows/i686"
macos/x86_64:
	@$(MAKE) -s --no-print-directory TARGET=macos/x86_64 CC=$(OSXCROSS_ROOT)/bin/o64-clang build
	@echo "OK macos/x86_64"
macos/aarch64:
	@$(MAKE) -s --no-print-directory TARGET=macos/aarch64 CC=$(OSXCROSS_ROOT)/bin/oa64-clang build
	@echo "OK macos/aarch64"

test:
	@if [ -n "$(filter wine,$(MAKECMDGOALS))" ]; then \
		if ! command -v $(WINE) >/dev/null 2>&1; then \
			echo "Missing Wine runtime: $(WINE)" >&2; exit 1; \
		fi; \
		if ! command -v $(WINE_X86_64_CC) >/dev/null 2>&1; then \
			echo "Missing Windows cross-compiler: $(WINE_X86_64_CC)" >&2; exit 1; \
		fi; \
		if [ ! -f $(BIN_DIR)/windows/x86_64/kc.exe ]; then \
			echo "Missing Windows artifacts. Run 'make windows/x86_64' or 'make all' first." >&2; exit 1; \
		fi; \
		mkdir -p $(BUILD_DIR)/test-wine; \
		$(WINE_X86_64_CC) -std=c11 $(CFLAGS) \
			-DKC_CLI_PATH='"kc.exe"' src/test.c \
			-o $(BUILD_DIR)/test-wine/kc_contract_test.exe || exit 1; \
		cp $(BIN_DIR)/windows/x86_64/kc.exe $(BUILD_DIR)/test-wine/kc.exe || exit 1; \
		(cd $(BUILD_DIR)/test-wine && WINEDEBUG=-all $(WINE) ./kc_contract_test.exe all); \
	else \
		if [ "$(NATIVE_PLATFORM)" = unsupported ] || [ "$(NATIVE_ARCH)" = unsupported ]; then \
			echo "Unsupported native test target $(HOST_MACHINE)/$(HOST_SYSTEM)" >&2; exit 1; \
		fi; \
		$(MAKE) -s --no-print-directory TARGET=$(NATIVE_TARGET) build || exit 1; \
		mkdir -p $(BUILD_DIR)/test; \
		$(CC) $(CFLAGS) -std=c11 -DKC_CLI_PATH='"$(CURDIR)/$(NATIVE_OUTPUT)"' \
			src/test.c -o $(BUILD_DIR)/test/kc_contract_test || exit 1; \
		(cd $(BUILD_DIR)/test && ./kc_contract_test all); \
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
