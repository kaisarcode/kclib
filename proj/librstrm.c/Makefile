## Makefile
## Summary: Cross-compilation builder for rstrm.
##
## Author:  KaisarCode
## Website: https://kaisarcode.com
## License: https://www.gnu.org/licenses/lgpl-3.0.html

SHELL := /bin/sh
ANDROID_HOME ?= $(HOME)/.local/share/android-sdk
NDK_VERSION ?= 27.2.12479018
NDK_TOOLCHAIN := $(ANDROID_HOME)/ndk/$(NDK_VERSION)/build/cmake/android.toolchain.cmake
XDG_DATA_HOME ?= $(HOME)/.local/share
OSXCROSS_ROOT ?= $(XDG_DATA_HOME)/osxcross/target
MACOSX_DEPLOYMENT_TARGET ?= 11.0
IOS_DEPLOYMENT_TARGET ?= 13.0
MACOSX_SDK ?= $(shell ls -d "$(OSXCROSS_ROOT)"/SDK/MacOSX*.sdk 2>/dev/null | sort -V | tail -n 1)
IPHONEOS_SDK ?= $(shell ls -d "$(OSXCROSS_ROOT)"/SDK/iPhoneOS*.sdk 2>/dev/null | sort -V | tail -n 1)
IPHONESIMULATOR_SDK ?= $(shell ls -d "$(OSXCROSS_ROOT)"/SDK/iPhoneSimulator*.sdk 2>/dev/null | sort -V | tail -n 1)
WINE ?= wine
WINE_X86_64_CC ?= x86_64-w64-mingw32-gcc
BUILD_DIR := .build
BIN_DIR := bin
BUILD_VERSION ?= $(shell date +%s)
CMAKE ?= cmake

define cmake_build
	@prelog=$$(mktemp); \
	if ! $(CMAKE) --build $(1) -- -n > "$$prelog" 2>&1; then \
		cat "$$prelog"; rm -f "$$prelog"; exit 1; \
	fi; \
	if grep -q "ninja: no work to do." "$$prelog"; then \
		rm -f "$$prelog"; \
		out=$$(mktemp); $(CMAKE) --build $(1) 2>"$$out"; r=$$?; \
		if [ -s "$$out" ]; then grep -v 'skipping incompatible' < "$$out"; fi; \
		rm -f "$$out"; exit $$r; \
	fi; \
	rm -f "$$prelog"; \
	out=$$(mktemp); $(CMAKE) --build $(1) 2>"$$out"; r=$$?; \
	if [ -s "$$out" ]; then grep -v 'skipping incompatible' < "$$out"; fi; \
	rm -f "$$out"; if [ $$r -ne 0 ]; then exit 1; fi; \
	ver=$$(date +%s); $(2); \
	log=$$(mktemp); if ! $(CMAKE) --build $(1) > "$$log" 2>&1; then cat "$$log"; rm -f "$$log"; exit 1; fi; \
	rm -f "$$log"; :
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
NATIVE_TARGET := $(NATIVE_ARCH)/$(NATIVE_PLATFORM)
NATIVE_SHARED_NAME := librstrm.so
NATIVE_IMPORT_LIBRARY :=
ifeq ($(NATIVE_PLATFORM),windows)
NATIVE_SHARED_NAME := librstrm.dll
NATIVE_IMPORT_LIBRARY := -DRSTRM_TEST_IMPORT_LIBRARY=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/librstrm.dll.a
endif
ifeq ($(NATIVE_PLATFORM),macos)
NATIVE_SHARED_NAME := librstrm.dylib
endif

.DEFAULT_GOAL := $(NATIVE_TARGET)
.PHONY: all test wine clean x86_64/linux x86_64/windows x86_64/macos x86_64/iossim i686/linux i686/windows aarch64/linux aarch64/android aarch64/macos aarch64/ios aarch64/iossim armv7/linux armv7/android armv7hf/linux riscv64/linux powerpc64le/linux mips/linux mipsel/linux mips64el/linux s390x/linux loongarch64/linux
all: x86_64/linux x86_64/windows x86_64/macos x86_64/iossim i686/linux i686/windows aarch64/linux aarch64/android aarch64/macos aarch64/ios aarch64/iossim armv7/linux armv7/android armv7hf/linux riscv64/linux powerpc64le/linux mips/linux mipsel/linux mips64el/linux s390x/linux loongarch64/linux

define build_target
	@mkdir -p $(BIN_DIR)/$(1)/$(2)
	@if [ ! -f $(BUILD_DIR)/$(1)-$(2)/CMakeCache.txt ]; then $(CMAKE) -S . -B $(BUILD_DIR)/$(1)-$(2) -DCMAKE_BUILD_TYPE=Release $(3) -DRSTRM_BUILD_VERSION=$(BUILD_VERSION) -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-$(2)/out -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/$(2) -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/$(2) -G Ninja -Wno-dev > /dev/null; fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-$(2),$(CMAKE) -S . -B $(BUILD_DIR)/$(1)-$(2) -DCMAKE_BUILD_TYPE=Release $(3) -DRSTRM_BUILD_VERSION=$$ver -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-$(2)/out -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/$(2) -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/$(2) -G Ninja -Wno-dev > /dev/null)
	@cp src/librstrm.h $(BIN_DIR)/$(1)/$(2)/librstrm.h
	@if [ "$(2)" = windows ]; then cp $(BUILD_DIR)/$(1)-$(2)/out/librstrm.dll $(BIN_DIR)/$(1)/$(2)/librstrm.dll; fi
	@echo "OK $(1)/$(2)"
endef

define linux_target
$(call build_target,$(1),linux,-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_C_COMPILER=$(2))
endef
x86_64/linux: ; $(call linux_target,x86_64,gcc)
i686/linux: ; $(call linux_target,i686,i686-linux-gnu-gcc)
aarch64/linux: ; $(call linux_target,aarch64,aarch64-linux-gnu-gcc)
armv7/linux: ; $(call linux_target,armv7,arm-linux-gnueabi-gcc)
armv7hf/linux: ; $(call linux_target,armv7hf,arm-linux-gnueabihf-gcc)
riscv64/linux: ; $(call linux_target,riscv64,riscv64-linux-gnu-gcc)
powerpc64le/linux: ; $(call linux_target,powerpc64le,powerpc64le-linux-gnu-gcc)
mips/linux: ; $(call linux_target,mips,mips-linux-gnu-gcc)
mipsel/linux: ; $(call linux_target,mipsel,mipsel-linux-gnu-gcc)
mips64el/linux: ; $(call linux_target,mips64el,mips64el-linux-gnuabi64-gcc)
s390x/linux: ; $(call linux_target,s390x,s390x-linux-gnu-gcc)
loongarch64/linux: ; $(call linux_target,loongarch64,loongarch64-linux-gnu-gcc)

define windows_target
$(call build_target,$(1),windows,-DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER=$(2))
endef
x86_64/windows: ; $(call windows_target,x86_64,x86_64-w64-mingw32-gcc)
i686/windows: ; $(call windows_target,i686,i686-w64-mingw32-gcc)

ifeq ($(HOST_SYSTEM),Darwin)
define macos_target
$(call build_target,$(1),macos,-DCMAKE_OSX_ARCHITECTURES=$(if $(filter aarch64,$(1)),arm64,$(1)))
endef
else
define macos_target
	@if ! $(2) --version >/dev/null 2>&1; then echo "Missing macOS cross-compiler wrapper: $(2)" >&2; exit 1; fi
$(call build_target,$(1),macos,-DCMAKE_SYSTEM_NAME=Darwin -DCMAKE_OSX_DEPLOYMENT_TARGET=$(MACOSX_DEPLOYMENT_TARGET) -DCMAKE_C_COMPILER=$(2) -DCMAKE_AR=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ar -DCMAKE_RANLIB=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ranlib)
endef
endif
x86_64/macos: ; $(call macos_target,x86_64,$(OSXCROSS_ROOT)/bin/o64-clang)
aarch64/macos: ; $(call macos_target,aarch64,$(OSXCROSS_ROOT)/bin/oa64-clang)

define ios_target
	@if ! $(3) --version >/dev/null 2>&1 || [ ! -d "$(5)" ]; then echo "Missing iOS compiler or SDK" >&2; exit 1; fi
	$(call build_target,$(1),$(2),-DCMAKE_SYSTEM_NAME=iOS -DCMAKE_SYSTEM_VERSION=$(IOS_DEPLOYMENT_TARGET) -DCMAKE_OSX_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) -DCMAKE_OSX_SYSROOT=$(5) -DCMAKE_OSX_ARCHITECTURES=$(6) -DCMAKE_C_COMPILER=$(3) -DCMAKE_AR=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ar -DCMAKE_RANLIB=$(OSXCROSS_ROOT)/bin/$(1)-apple-darwin25.1-ranlib)
endef
aarch64/ios: ; $(call ios_target,aarch64,ios,$(OSXCROSS_ROOT)/bin/ios64-clang,IPHONEOS_SDK,$(IPHONEOS_SDK),arm64)
aarch64/iossim: ; $(call ios_target,aarch64,iossim,$(OSXCROSS_ROOT)/bin/iossim64-clang,IPHONESIMULATOR_SDK,$(IPHONESIMULATOR_SDK),arm64)
x86_64/iossim: ; $(call ios_target,x86_64,iossim,$(OSXCROSS_ROOT)/bin/iossimx64-clang,IPHONESIMULATOR_SDK,$(IPHONESIMULATOR_SDK),x86_64)

define android_target
$(call build_target,$(1),android,-DCMAKE_TOOLCHAIN_FILE=$(NDK_TOOLCHAIN) -DANDROID_ABI=$(2) -DANDROID_PLATFORM=android-21)
endef
aarch64/android: ; $(call android_target,aarch64,arm64-v8a)
armv7/android: ; $(call android_target,armv7,armeabi-v7a)

test:
	@if [ -n "$(filter wine,$(MAKECMDGOALS))" ]; then \
		if ! command -v $(WINE) >/dev/null || ! command -v $(WINE_X86_64_CC) >/dev/null; then echo "Missing Wine or Windows cross-compiler" >&2; exit 1; fi; \
		if [ ! -f $(BIN_DIR)/x86_64/windows/librstrm.dll ] || [ ! -f $(BIN_DIR)/x86_64/windows/librstrm.dll.a ]; then echo "Missing Windows artifacts. Run 'make x86_64/windows' first." >&2; exit 1; fi; \
		cmake -S . -B $(BUILD_DIR)/test-wine -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER=$(WINE_X86_64_CC) -DRSTRM_BUILD_TESTS=ON -DRSTRM_BUILD_VERSION=$(BUILD_VERSION) -DRSTRM_TEST_SHARED_LIBRARY=$(CURDIR)/$(BIN_DIR)/x86_64/windows/librstrm.dll -DRSTRM_TEST_IMPORT_LIBRARY=$(CURDIR)/$(BIN_DIR)/x86_64/windows/librstrm.dll.a -DCMAKE_CROSSCOMPILING_EMULATOR=$(WINE) -G Ninja -Wno-dev > /dev/null && cmake --build $(BUILD_DIR)/test-wine --target rstrm_contract_test && WINEDEBUG=-all $(WINE) $(BUILD_DIR)/test-wine/rstrm_contract_test.exe all; \
	else \
		if [ ! -f $(BIN_DIR)/$(NATIVE_TARGET)/$(NATIVE_SHARED_NAME) ]; then echo "Missing native artifacts. Run 'make' first." >&2; exit 1; fi; \
		cmake -S . -B $(BUILD_DIR)/test -DCMAKE_BUILD_TYPE=Release -DRSTRM_BUILD_TESTS=ON -DRSTRM_BUILD_VERSION=$(BUILD_VERSION) -DRSTRM_TEST_SHARED_LIBRARY=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/$(NATIVE_SHARED_NAME) $(NATIVE_IMPORT_LIBRARY) -G Ninja -Wno-dev > /dev/null && cmake --build $(BUILD_DIR)/test --target rstrm_contract_test && $(BUILD_DIR)/test/rstrm_contract_test all; \
	fi
wine:
	@if [ -z "$(filter test,$(MAKECMDGOALS))" ]; then echo "Use 'make test wine'." >&2; exit 1; fi
clean:
	@rm -rf $(BUILD_DIR) $(BIN_DIR)
	@echo "OK clean"
