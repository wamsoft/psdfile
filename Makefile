SHELL = /bin/bash

ifeq ($(VCPKG_ROOT),)
$(error Variables VCPKG_ROOT not set correctly.)
endif

ifeq ($(shell type cygpath >& /dev/null && echo true),true)
FIXPATH = cygpath -ma
else
FIXPATH = realpath
endif

# Detect OS and set default PRESET accordingly
ifeq ($(OS),Windows_NT)
	PRESET?=x64-windows
else
	UNAME_S := $(shell uname -s)
	UNAME_M := $(shell uname -m)
	ifeq ($(UNAME_S),Linux)
		ifeq ($(UNAME_M),aarch64)
			PRESET?=arm64-linux
		else
			PRESET?=x64-linux
		endif
	else ifeq ($(UNAME_S),Darwin)
		ifeq ($(UNAME_M),arm64)
			PRESET?=arm64-osx
		else
			PRESET?=x64-osx
		endif
	else
		PRESET?=x64-windows
	endif
endif

BUILD_TYPE?=Release
CMAKEOPT?=""
INSTALL_PREFIX?=install

BUILD_PATH=$(shell cmake --preset $(PRESET) -N | grep BUILD_DIR | sed 's/.*BUILD_DIR="\(.*\)"/\1/')

.PHONY: prebuild build clean install run

all: build

# cmake 処理実行
# CMAKEOPT で引数定義追加
prebuild:
	cmake --preset $(PRESET) ${CMAKEOPT}
# ビルド実行
build:
	cmake --build $(BUILD_PATH) --config $(BUILD_TYPE)

clean:
	cmake --build $(BUILD_PATH) --config $(BUILD_TYPE) --target clean

install:
	cmake --install $(BUILD_PATH) --config $(BUILD_TYPE) --prefix $(INSTALL_PREFIX)

