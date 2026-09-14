## Makefile
## Summary: Cross-compilation builder for hnsw.
##
## Author:  KaisarCode
## Website: https://kaisarcode.com
## License: https://www.gnu.org/licenses/gpl-3.0.html

ANDROID_HOME  ?= $(HOME)/.local/share/android-sdk
NDK_VERSION   ?= 27.2.12479018
NDK_DIR       := $(ANDROID_HOME)/ndk/$(NDK_VERSION)
NDK_TOOLCHAIN := $(NDK_DIR)/build/cmake/android.toolchain.cmake

BUILD_DIR := .build
BIN_DIR   := bin
BUILD_VERSION ?= $(shell date +%s)
WINE ?= wine
WINE_X86_64_CC ?= x86_64-w64-mingw32-gcc
WASM_EMCMAKE ?= emcmake
WASM_EMCC    ?= emcc
WASM_NODE    ?= node

define cmake_build
	@prelog=$$(mktemp); \
	if ! cmake --build $(1) -- -n > "$$prelog" 2>&1; then \
		cat "$$prelog"; \
		rm -f "$$prelog"; \
		exit 1; \
	fi; \
	if grep -q "ninja: no work to do." "$$prelog"; then \
		rm -f "$$prelog"; \
		out=$$(mktemp); \
		cmake --build $(1) 2>"$$out"; \
		r=$$?; \
		if [ -s "$$out" ]; then grep -v 'skipping incompatible' < "$$out"; fi; \
		rm -f "$$out"; \
		exit $$r; \
	fi; \
	rm -f "$$prelog"; \
	out=$$(mktemp); \
	cmake --build $(1) 2>"$$out"; \
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
		if ! cmake --build $(1) > "$$log" 2>&1; then \
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

.DEFAULT_GOAL := $(NATIVE_TARGET)

.PHONY: all test wine clean \
	wasm wasm32/wasm \
	x86_64/linux x86_64/windows \
	i686/linux i686/windows \
	aarch64/linux aarch64/android \
	armv7/linux armv7/android \
	armv7hf/linux \
	riscv64/linux \
	powerpc64le/linux \
	mips/linux mipsel/linux mips64el/linux \
	s390x/linux \
	loongarch64/linux

all: \
	x86_64/linux x86_64/windows \
	i686/linux i686/windows \
	aarch64/linux aarch64/android \
	armv7/linux armv7/android \
	armv7hf/linux \
	riscv64/linux \
	powerpc64le/linux \
	mips/linux mipsel/linux mips64el/linux \
	s390x/linux \
	loongarch64/linux \
	wasm32/wasm

## Linux
## $(1)=target  $(2)=processor  $(3)=CC

define linux_target
	@mkdir -p $(BIN_DIR)/$(1)/linux
	@if [ ! -f $(BUILD_DIR)/$(subst /,-,$(1))-linux/CMakeCache.txt ]; then \
		cmake -S . -B $(BUILD_DIR)/$(subst /,-,$(1))-linux \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Linux \
			-DCMAKE_SYSTEM_PROCESSOR=$(2) \
			-DHNSW_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_C_COMPILER=$(3) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(subst /,-,$(1))-linux/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(subst /,-,$(1))-linux,cmake -S . -B $(BUILD_DIR)/$(subst /,-,$(1))-linux -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=$(2) -DHNSW_BUILD_VERSION=$$ver -DCMAKE_C_COMPILER=$(3) -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(subst /,-,$(1))-linux/out -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux -G Ninja -Wno-dev > /dev/null)
	@cp $(BUILD_DIR)/$(subst /,-,$(1))-linux/out/hnsw $(BIN_DIR)/$(1)/linux/hnsw
	@echo "OK $(1)/linux"
endef

x86_64/linux:
	$(call linux_target,x86_64,x86_64,gcc)

i686/linux:
	$(call linux_target,i686,i686,i686-linux-gnu-gcc)

aarch64/linux:
	$(call linux_target,aarch64,aarch64,aarch64-linux-gnu-gcc)

armv7hf/linux:
	$(call linux_target,armv7hf,armv7,arm-linux-gnueabihf-gcc)

armv7/linux:
	$(call linux_target,armv7,armv7,arm-linux-gnueabi-gcc)

riscv64/linux:
	$(call linux_target,riscv64,riscv64,riscv64-linux-gnu-gcc)

powerpc64le/linux:
	$(call linux_target,powerpc64le,powerpc64le,powerpc64le-linux-gnu-gcc)

mips/linux:
	$(call linux_target,mips,mips,mips-linux-gnu-gcc)

mipsel/linux:
	$(call linux_target,mipsel,mipsel,mipsel-linux-gnu-gcc)

mips64el/linux:
	$(call linux_target,mips64el,mips64el,mips64el-linux-gnuabi64-gcc)

s390x/linux:
	$(call linux_target,s390x,s390x,s390x-linux-gnu-gcc)

loongarch64/linux:
	$(call linux_target,loongarch64,loongarch64,loongarch64-linux-gnu-gcc)

## Windows
## $(1)=target  $(2)=processor  $(3)=CC

define windows_target
	@mkdir -p $(BIN_DIR)/$(1)/windows
	@if [ ! -f $(BUILD_DIR)/$(1)-windows/CMakeCache.txt ]; then \
		cmake -S . -B $(BUILD_DIR)/$(1)-windows \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Windows \
			-DCMAKE_SYSTEM_PROCESSOR=$(2) \
			-DHNSW_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_C_COMPILER=$(3) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-windows/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-windows,cmake -S . -B $(BUILD_DIR)/$(1)-windows -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=$(2) -DHNSW_BUILD_VERSION=$$ver -DCMAKE_C_COMPILER=$(3) -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-windows/out -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows -G Ninja -Wno-dev > /dev/null)
	@cp $(BUILD_DIR)/$(1)-windows/out/hnsw.exe $(BIN_DIR)/$(1)/windows/hnsw.exe
	@cp $(BUILD_DIR)/$(1)-windows/out/libhnsw.dll $(BIN_DIR)/$(1)/windows/libhnsw.dll
	@echo "OK $(1)/windows"
endef

x86_64/windows:
	$(call windows_target,x86_64,x86_64,x86_64-w64-mingw32-gcc)

i686/windows:
	$(call windows_target,i686,i686,i686-w64-mingw32-gcc)

## Android

define android_target
	@mkdir -p $(BIN_DIR)/$(1)/android
	@cache=$(BUILD_DIR)/$(1)-android/CMakeCache.txt && \
	if [ -f "$$cache" ] && { ! grep -q '^CMAKE_TOOLCHAIN_FILE:.*=$(NDK_TOOLCHAIN)$$' "$$cache" || ! grep -q '^ANDROID_ABI:.*=$(2)$$' "$$cache"; }; then \
		rm -f "$$cache" $(BUILD_DIR)/$(1)-android/build.ninja && rm -rf $(BUILD_DIR)/$(1)-android/CMakeFiles; \
	fi
	@if [ ! -f $(BUILD_DIR)/$(1)-android/CMakeCache.txt ]; then \
		cmake -S . -B $(BUILD_DIR)/$(1)-android \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_TOOLCHAIN_FILE=$(NDK_TOOLCHAIN) \
			-DHNSW_BUILD_VERSION=$(BUILD_VERSION) \
			-DANDROID_ABI=$(2) \
			-DANDROID_PLATFORM=android-21 \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-android/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/android \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/android \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-android,cmake -S . -B $(BUILD_DIR)/$(1)-android -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$(NDK_TOOLCHAIN) -DHNSW_BUILD_VERSION=$$ver -DANDROID_ABI=$(2) -DANDROID_PLATFORM=android-21 -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-android/out -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/android -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/android -G Ninja -Wno-dev > /dev/null)
	@cp $(BUILD_DIR)/$(1)-android/out/hnsw $(BIN_DIR)/$(1)/android/hnsw
	@echo "OK $(1)/android"
endef

aarch64/android:
	$(call android_target,aarch64,arm64-v8a)

armv7/android:
	$(call android_target,armv7,armeabi-v7a)

## WebAssembly

define wasm_target
	@if ! command -v $(WASM_EMCMAKE) >/dev/null 2>&1 || ! command -v $(WASM_EMCC) >/dev/null 2>&1; then \
		echo "Missing Emscripten SDK toolchain: $(WASM_EMCMAKE)/$(WASM_EMCC)" >&2; \
		echo "Install the Emscripten SDK and add emcmake/emcc to PATH (e.g. 'source emsdk_env.sh')." >&2; \
		exit 1; \
	fi
	@mkdir -p $(BIN_DIR)/wasm32/wasm
	@if [ ! -f $(BUILD_DIR)/wasm32-wasm/CMakeCache.txt ]; then \
		$(WASM_EMCMAKE) cmake -S . -B $(BUILD_DIR)/wasm32-wasm \
			-DCMAKE_BUILD_TYPE=Release \
			-DHNSW_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/wasm32-wasm/out \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/wasm32-wasm,$(WASM_EMCMAKE) cmake -S . -B $(BUILD_DIR)/wasm32-wasm -DCMAKE_BUILD_TYPE=Release -DHNSW_BUILD_VERSION=$$ver -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/wasm32-wasm/out -G Ninja -Wno-dev > /dev/null)
	@cp $(BUILD_DIR)/wasm32-wasm/out/hnsw.wasm $(BIN_DIR)/wasm32/wasm/hnsw.wasm
	@echo "OK wasm32/wasm"
endef

wasm32/wasm:
	$(call wasm_target)

## Utility

test:
	@if [ -n "$(filter wine,$(MAKECMDGOALS))" ]; then \
		if ! command -v wine > /dev/null 2>&1; then \
			echo "wine is required for make test wine" >&2; \
			exit 1; \
		fi; \
		if ! command -v x86_64-w64-mingw32-gcc > /dev/null 2>&1; then \
			echo "x86_64-w64-mingw32-gcc is required for make test wine" >&2; \
			exit 1; \
		fi; \
		if [ ! -f "$(CURDIR)/$(BIN_DIR)/x86_64/windows/libhnsw.dll" ]; then \
			echo "missing Windows shared library: run make x86_64/windows first" >&2; \
			exit 1; \
		fi; \
		if [ ! -f "$(CURDIR)/$(BIN_DIR)/x86_64/windows/libhnsw.dll.a" ]; then \
			echo "missing Windows import library: run make x86_64/windows first" >&2; \
			exit 1; \
		fi; \
		if [ ! -f "$(CURDIR)/$(BIN_DIR)/x86_64/windows/hnsw.exe" ]; then \
			echo "missing Windows CLI: run make x86_64/windows first" >&2; \
			exit 1; \
		fi; \
		if [ ! -f "$(BUILD_DIR)/test-wine/CMakeCache.txt" ]; then \
			cmake -S . -B $(BUILD_DIR)/test-wine \
				-DCMAKE_BUILD_TYPE=Release \
				-DCMAKE_SYSTEM_NAME=Windows \
				-DCMAKE_C_COMPILER=$(WINE_X86_64_CC) \
				-DCMAKE_CROSSCOMPILING_EMULATOR=$(WINE) \
				-DHNSW_BUILD_TESTS=ON \
				-DHNSW_TEST_SHARED_LIBRARY=$(CURDIR)/$(BIN_DIR)/x86_64/windows/libhnsw.dll \
				-DHNSW_TEST_IMPORT_LIBRARY=$(CURDIR)/$(BIN_DIR)/x86_64/windows/libhnsw.dll.a \
				-DHNSW_TEST_CLI=$(CURDIR)/$(BIN_DIR)/x86_64/windows/hnsw.exe \
				-G Ninja -Wno-dev > /dev/null; \
		fi; \
		cmake --build $(BUILD_DIR)/test-wine --target hnsw_contract_test; \
		EGL_LOG_LEVEL=fatal WINEDEBUG=-all $(WINE) $(BUILD_DIR)/test-wine/hnsw_contract_test.exe all; \
	elif [ -n "$(filter wasm,$(MAKECMDGOALS))" ]; then \
		if ! command -v $(WASM_EMCMAKE) >/dev/null 2>&1 || ! command -v $(WASM_EMCC) >/dev/null 2>&1; then \
			echo "Missing Emscripten SDK toolchain: $(WASM_EMCMAKE)/$(WASM_EMCC)" >&2; \
			echo "Install the Emscripten SDK and add emcmake/emcc to PATH (e.g. 'source emsdk_env.sh')." >&2; \
			exit 1; \
		fi; \
		if [ ! -f $(BIN_DIR)/wasm32/wasm/hnsw.wasm ]; then \
			echo "Missing $(BIN_DIR)/wasm32/wasm/hnsw.wasm. Run 'make wasm32/wasm' first." >&2; \
			exit 1; \
		fi; \
		if [ ! -f $(BUILD_DIR)/test-wasm/CMakeCache.txt ]; then \
			$(WASM_EMCMAKE) cmake -S . -B $(BUILD_DIR)/test-wasm \
				-DCMAKE_BUILD_TYPE=Release \
				-DHNSW_BUILD_TESTS=ON \
				-DHNSW_BUILD_VERSION=$(BUILD_VERSION) \
				-G Ninja -Wno-dev > /dev/null; \
		fi; \
		cmake --build $(BUILD_DIR)/test-wasm --target hnsw_contract_test || exit 1; \
		if [ -n "$(EMSDK_NODE)" ]; then node="$(EMSDK_NODE)"; else node="$(WASM_NODE)"; fi; \
		if ! command -v "$$node" >/dev/null 2>&1; then \
			echo "Missing Emscripten test runtime: $$node" >&2; \
			exit 1; \
		fi; \
		"$$node" $(BUILD_DIR)/test-wasm/hnsw_contract_test.js all; \
	else \
		if [ "$(NATIVE_ARCH)" = "unsupported" ] || [ "$(NATIVE_PLATFORM)" = "unsupported" ]; then \
			echo "Unsupported native target $(HOST_ARCH)/$(HOST_SYSTEM)" >&2; \
			exit 1; \
		fi; \
		if [ ! -f "$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/libhnsw.so" ]; then \
			echo "missing native shared library: run make first" >&2; \
			exit 1; \
		fi; \
		if [ ! -f "$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/hnsw" ]; then \
			echo "missing native CLI: run make first" >&2; \
			exit 1; \
		fi; \
		if [ ! -f "$(BUILD_DIR)/test/CMakeCache.txt" ]; then \
			cmake -S . -B $(BUILD_DIR)/test \
				-DCMAKE_BUILD_TYPE=Release \
				-DHNSW_BUILD_TESTS=ON \
				-DHNSW_TEST_SHARED_LIBRARY=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/libhnsw.so \
				-DHNSW_TEST_CLI=$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET)/hnsw \
				-G Ninja -Wno-dev > /dev/null; \
		fi; \
		cmake --build $(BUILD_DIR)/test --target hnsw_contract_test; \
		$(BUILD_DIR)/test/hnsw_contract_test all; \
	fi

wine:
	@if [ -z "$(filter test,$(MAKECMDGOALS))" ]; then \
		echo "Use 'make test wine' to run tests through Wine." >&2; \
		exit 1; \
	fi

wasm:
	@if [ -z "$(filter test,$(MAKECMDGOALS))" ]; then \
		echo "Use 'make test wasm' to run tests through Emscripten and Node.js." >&2; \
		exit 1; \
	fi

clean:
	@rm -rf $(BUILD_DIR)
	@echo "OK clean"
