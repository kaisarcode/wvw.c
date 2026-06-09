## Makefile
## Summary: Cross-compilation builder for wvw.
##
## Author:  KaisarCode
## Website: https://kaisarcode.com
## License: https://www.gnu.org/licenses/gpl-3.0.html

BUILD_DIR := .build
BIN_DIR   := bin

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
	@$(MAKE) $(NATIVE_TARGET)

all: x86_64/linux x86_64/windows

## Linux

define linux_target
	@mkdir -p $(BIN_DIR)/$(1)/linux
	@cmake -S . -B $(BUILD_DIR)/$(subst /,-,$(1))-linux \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_SYSTEM_NAME=Linux \
		-DCMAKE_C_COMPILER=$(2) \
		-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(subst /,-,$(1))-linux/out \
		-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux \
		-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/linux \
		-G Ninja -Wno-dev > /dev/null
	@cmake --build $(BUILD_DIR)/$(subst /,-,$(1))-linux
	@cp $(BUILD_DIR)/$(subst /,-,$(1))-linux/out/wvw $(BIN_DIR)/$(1)/linux/wvw
	@echo "OK $(1)/linux"
endef

x86_64/linux:
	$(call linux_target,x86_64,gcc)

## Windows

define windows_target
	@mkdir -p $(BIN_DIR)/$(1)/windows
	@cmake -S . -B $(BUILD_DIR)/$(1)-windows \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_SYSTEM_NAME=Windows \
		-DCMAKE_C_COMPILER=$(2) \
		-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(CURDIR)/$(BUILD_DIR)/$(1)-windows/out \
		-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows \
		-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/windows \
		-G Ninja -Wno-dev > /dev/null
	@cmake --build $(BUILD_DIR)/$(1)-windows
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
