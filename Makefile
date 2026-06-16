## Makefile
## Summary: Cross-compilation builder for wvw.
##
## Author:  KaisarCode
## Website: https://kaisarcode.com
## License: https://www.gnu.org/licenses/gpl-3.0.html

BUILD_DIR := .build
BIN_DIR   := bin
BUILD_VERSION ?= $(shell date +%s)

define cmake_build
	@prelog=$$(mktemp); \
	if ! cmake --build $(1) -- -n > "$$prelog" 2>&1; then \
		cat "$$prelog"; \
		rm -f "$$prelog"; \
		exit 1; \
	fi; \
	if grep -q "ninja: no work to do." "$$prelog"; then \
		rm -f "$$prelog"; \
		cmake --build $(1); \
		exit $$?; \
	fi; \
	rm -f "$$prelog"; \
	if ! cmake --build $(1); then \
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

ifeq ($(HOST_SYSTEM),Linux)
NATIVE_PLATFORM := linux
endif

ifneq ($(filter MINGW% MSYS% CYGWIN%,$(HOST_SYSTEM)),)
NATIVE_PLATFORM := windows
endif

NATIVE_TARGET := $(NATIVE_ARCH)/$(NATIVE_PLATFORM)

.DEFAULT_GOAL := native

.PHONY: native all test clean \
	x86_64/linux x86_64/windows

native:
	@if [ "$(NATIVE_ARCH)" = "unsupported" ] || [ "$(NATIVE_PLATFORM)" = "unsupported" ]; then \
		echo "Unsupported native target $(HOST_ARCH)/$(HOST_SYSTEM)" >&2; \
		exit 1; \
	fi
	@$(MAKE) BUILD_VERSION=$(BUILD_VERSION) $(NATIVE_TARGET)

all: x86_64/linux x86_64/windows

## Linux

define linux_target
	@mkdir -p $(BIN_DIR)/$(1)/linux
	@if [ ! -f $(BUILD_DIR)/$(subst /,-,$(1))-linux/CMakeCache.txt ]; then \
		cmake -S . -B $(BUILD_DIR)/$(subst /,-,$(1))-linux \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SYSTEM_NAME=Linux \
			-DWVW_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_C_COMPILER=$(2) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(subst /,-,$(1))-linux/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(subst /,-,$(1))-linux,cmake -S . -B $(BUILD_DIR)/$(subst /,-,$(1))-linux -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Linux -DWVW_BUILD_VERSION=$$ver -DCMAKE_C_COMPILER=$(2) -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(subst /,-,$(1))-linux/out -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux -G Ninja -Wno-dev > /dev/null)
	@cp $(BUILD_DIR)/$(subst /,-,$(1))-linux/out/wvw $(BIN_DIR)/$(1)/linux/wvw
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
			-DWVW_BUILD_VERSION=$(BUILD_VERSION) \
			-DCMAKE_C_COMPILER=$(2) \
			-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-windows/out \
			-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-windows,cmake -S . -B $(BUILD_DIR)/$(1)-windows -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Windows -DWVW_BUILD_VERSION=$$ver -DCMAKE_C_COMPILER=$(2) -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-windows/out -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows -G Ninja -Wno-dev > /dev/null)
	@cp $(BUILD_DIR)/$(1)-windows/out/wvw.exe $(BIN_DIR)/$(1)/windows/wvw.exe
	@cp $(BUILD_DIR)/$(1)-windows/out/libwvw.dll $(BIN_DIR)/$(1)/windows/libwvw.dll
	@echo "OK $(1)/windows"
endef

x86_64/windows:
	$(call windows_target,x86_64,x86_64-w64-mingw32-gcc)

## Utility

test:
	@sh test.sh

clean:
	@rm -rf $(BUILD_DIR)
	@echo "OK clean"
