# OS detection. Not used in CI builds
ifndef TARGET_OS
ifeq ($(OS),Windows_NT)
	TARGET_OS := win32
else
	UNAME_S := $(shell uname -s)
	ifeq ($(UNAME_S),Linux)
		UNAME_M := $(shell uname -m)
		ifeq ($(UNAME_M),x86_64)
			TARGET_OS := linux64
		endif
		ifeq ($(UNAME_M),i686)
			TARGET_OS := linux32
		endif
		ifeq ($(UNAME_M),armv6l)
			TARGET_OS := linux-armhf
		endif
	endif
	ifeq ($(UNAME_S),Darwin)
		TARGET_OS := osx
	endif
	ifeq ($(UNAME_S),FreeBSD)
		TARGET_OS := freebsd
	endif
endif
endif # TARGET_OS

# OS-specific settings and build flags
ifeq ($(TARGET_OS),windows)
	ARCHIVE ?= zip
	TARGET := mklittlefs.exe
	TARGET_CFLAGS = -mno-ms-bitfields
	TARGET_LDFLAGS = -Wl,-static -static-libgcc -static-libstdc++
else
	ARCHIVE ?= tar
	TARGET := mklittlefs
endif

# Packaging into archive (for 'dist' target)
ifeq ($(ARCHIVE), zip)
	ARCHIVE_CMD := zip -r
	ARCHIVE_EXTENSION := zip
endif
ifeq ($(ARCHIVE), tar)
	ARCHIVE_CMD := tar czf
	ARCHIVE_EXTENSION := tar.gz
endif

STRIP ?= strip

VERSION ?= $(shell git describe --always)
LITTLEFS_VERSION := $(shell git -C ../src/littlefs describe --tags || echo "unknown")
BUILD_CONFIG_NAME ?= -generic

OBJ		:= main.o \
		   ../src/littlefs/lfs.o \
		   ../src/littlefs/lfs_util.o

INCLUDES := -Itclap -Iinclude -Ilittlefs -I. -I ../src

FILES_TO_FORMAT := $(shell find . -not -path './littlefs/*' \( -name '*.c' -o -name '*.cpp' \))

DIFF_FILES := $(addsuffix .diff,$(FILES_TO_FORMAT))

# clang doesn't seem to handle -D "ARG=\"foo bar\"" correctly, so replace spaces with \x20:
BUILD_CONFIG_STR := $(shell echo $(CPPFLAGS) | sed 's- -\\\\x20-g')

override CPPFLAGS := \
	$(INCLUDES) \
	-D VERSION=\"$(VERSION)\" \
	-D LITTLEFS_VERSION=\"$(LITTLEFS_VERSION)\" \
	-D BUILD_CONFIG=\"$(BUILD_CONFIG_STR)\" \
	-D BUILD_CONFIG_NAME=\"$(BUILD_CONFIG_NAME)\" \
	-D __NO_INLINE__ \
	-D LFS_NAME_MAX=32 \
	$(CPPFLAGS)

override CFLAGS := -std=gnu99 -Os -Wall $(TARGET_CFLAGS) $(CFLAGS)
override CXXFLAGS := -std=gnu++11 -Os -Wall $(TARGET_CXXFLAGS) $(CXXFLAGS)
override LDFLAGS := $(TARGET_LDFLAGS) $(LDFLAGS)

DIST_NAME := mklittlefs-$(VERSION)$(BUILD_CONFIG_NAME)-$(TARGET_OS)
DIST_DIR := $(DIST_NAME)
DIST_ARCHIVE := $(DIST_NAME).$(ARCHIVE_EXTENSION)

all: $(TARGET)

dist: $(DIST_ARCHIVE)

$(DIST_ARCHIVE): $(TARGET) $(DIST_DIR)
	cp $(TARGET) $(DIST_DIR)/
	$(ARCHIVE_CMD) $(DIST_ARCHIVE) $(DIST_DIR)

$(TARGET): $(OBJ)
	$(CXX) $^ -o $@ $(LDFLAGS)
	$(STRIP) $(TARGET)

$(DIST_DIR):
	@mkdir -p $@

clean:
	@rm -f $(TARGET) $(OBJ) $(DIFF_FILES)

format-check: $(DIFF_FILES)
	@rm -f $(DIFF_FILES)

.PHONY: all clean dist format-check
