## Makefile
## Summary: Cross-compilation builder for menu.
##
## Author:  KaisarCode
## Website: https://kaisarcode.com
## License: https://www.gnu.org/licenses/gpl-3.0.html

WINE ?= wine
WINE_X86_64_CC ?= x86_64-w64-mingw32-gcc

BUILD_DIR := .build
BIN_DIR   := bin
BUILD_VERSION ?= $(shell date +%s)

define cmake_build
	@prelog=$(mktemp); \
	if ! $(3) cmake --build $(1) -- -n > "$prelog" 2>&1; then \
		cat "$prelog"; \
		rm -f "$prelog"; \
		exit 1; \
	fi; \
	if grep -q "ninja: no work to do." "$prelog"; then \
		rm -f "$prelog"; \
		out=$(mktemp); \
		$(3) cmake --build $(1) 2>"$out"; \
		r=$?; \
		if [ -s "$out" ]; then grep -v 'skipping incompatible' < "$out"; fi; \
		rm -f "$out"; \
		exit $r; \
	fi; \
	rm -f "$prelog"; \
	out=$(mktemp); \
	$(3) cmake --build $(1) 2>"$out"; \
	r=$?; \
	if [ -s "$out" ]; then grep -v 'skipping incompatible' < "$out"; fi; \
	rm -f "$out"; \
	if [ $r -ne 0 ]; then \
		exit 1; \
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
NATIVE_SHARED_NAME := libmenu.so
NATIVE_IMPORT_LIBRARY :=

ifeq ($(NATIVE_PLATFORM),windows)
NATIVE_EXE_EXT := .exe
NATIVE_SHARED_NAME := libmenu.dll
NATIVE_IMPORT_LIBRARY := -DMENU_TEST_IMPORT_LIBRARY=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/libmenu.dll.a
endif

.DEFAULT_GOAL := $(NATIVE_TARGET)

.PHONY: all test wine clean \
	x86_64/linux x86_64/windows i686/linux i686/windows \
	aarch64/linux armv7/linux armv7hf/linux riscv64/linux \
	powerpc64le/linux mips/linux mipsel/linux mips64el/linux \
	s390x/linux loongarch64/linux

all: \
	x86_64/linux x86_64/windows i686/linux i686/windows \
	aarch64/linux armv7/linux armv7hf/linux riscv64/linux \
	powerpc64le/linux mips/linux mipsel/linux mips64el/linux \
	s390x/linux loongarch64/linux

define linux_target
	@mkdir -p $(BIN_DIR)/$(1)/linux
	@cache=$(BUILD_DIR)/$(subst /,-,$(1))-linux/CMakeCache.txt; \
	if [ ! -f "$cache" ] || ! grep -q 'CMAKE_LIBRARY_OUTPUT_DIRECTORY:.*=$(CURDIR)/$(BIN_DIR)/$(1)/linux' "$cache"; then \
		cmake -S . -B $(BUILD_DIR)/$(subst /,-,$(1))-linux \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Linux \
			-DMENU_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_C_COMPILER=$(2) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(subst /,-,$(1))-linux,)
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

define windows_target
	@mkdir -p $(BIN_DIR)/$(1)/windows
	@cache=$(BUILD_DIR)/$(1)-windows/CMakeCache.txt; \
	if [ ! -f "$cache" ] || ! grep -q 'CMAKE_LIBRARY_OUTPUT_DIRECTORY:.*=$(CURDIR)/$(BIN_DIR)/$(1)/windows' "$cache"; then \
		cmake -S . -B $(BUILD_DIR)/$(1)-windows \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Windows \
			-DMENU_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_C_COMPILER=$(2) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-windows,)
	@echo "OK $(1)/windows"
endef

x86_64/windows:
	$(call windows_target,x86_64,x86_64-w64-mingw32-gcc)
i686/windows:
	$(call windows_target,i686,i686-w64-mingw32-gcc)

test:
	@if [ -n "$(filter wine,$(MAKECMDGOALS))" ]; then \
		test -f $(BIN_DIR)/x86_64/windows/libmenu.dll || { echo "Missing Windows artifacts. Run 'make x86_64/windows' first." >&2; exit 1; }; \
		cmake -S . -B $(BUILD_DIR)/test-wine \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Windows \
			-DCMAKE_C_COMPILER=$(WINE_X86_64_CC) \
			-DMENU_BUILD_TESTS=ON \
			-DMENU_TEST_SHARED_LIBRARY=$(CURDIR)/$(BIN_DIR)/x86_64/windows/libmenu.dll \
			-DMENU_TEST_IMPORT_LIBRARY=$(CURDIR)/$(BIN_DIR)/x86_64/windows/libmenu.dll.a \
			-DMENU_TEST_CLI=$(CURDIR)/$(BIN_DIR)/x86_64/windows/menu.exe \
			-DCMAKE_CROSSCOMPILING_EMULATOR=$(WINE) \
			-G Ninja -Wno-dev > /dev/null; \
		cmake --build $(BUILD_DIR)/test-wine --target menu_contract_test || exit 1; \
		WINEDEBUG=-all $(WINE) $(BUILD_DIR)/test-wine/menu_contract_test.exe all; \
	else \
		if [ "$(NATIVE_ARCH)" = "unsupported" ] || [ "$(NATIVE_PLATFORM)" = "unsupported" ]; then \
			echo "Unsupported native test target $(HOST_ARCH)/$(HOST_SYSTEM)" >&2; exit 1; \
		fi; \
		test -f $(BIN_DIR)/$(NATIVE_TARGET)/$(NATIVE_SHARED_NAME) || { echo "Missing native artifacts. Run 'make' first." >&2; exit 1; }; \
		cmake -S . -B $(BUILD_DIR)/test \
			-DCMAKE_BUILD_TYPE=Release \
			-DMENU_BUILD_TESTS=ON \
			-DMENU_TEST_SHARED_LIBRARY=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/$(NATIVE_SHARED_NAME) \
			-DMENU_TEST_CLI=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/menu$(NATIVE_EXE_EXT) \
			$(NATIVE_IMPORT_LIBRARY) \
			-G Ninja -Wno-dev > /dev/null; \
		cmake --build $(BUILD_DIR)/test --target menu_contract_test || exit 1; \
		$(BUILD_DIR)/test/menu_contract_test all; \
	fi

wine:
	@if [ -z "$(filter test,$(MAKECMDGOALS))" ]; then \
		echo "Use 'make test wine' to run tests through Wine." >&2; exit 1; \
	fi
	@:

clean:
	@rm -rf $(BUILD_DIR)
	@echo "OK clean"
