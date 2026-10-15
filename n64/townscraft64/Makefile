# Build with tools/build.py, which sets N64_INST to the pinned toolchain in
# build/deps and runs this from a path without spaces.
BUILD_DIR = build/rom
include $(N64_INST)/include/n64.mk

SRC = src/main.c

all: townscraft64.z64

$(BUILD_DIR)/townscraft64.elf: $(SRC:%.c=$(BUILD_DIR)/%.o)

townscraft64.z64: N64_ROM_TITLE = "Townscraft64"

clean:
	rm -rf $(BUILD_DIR) townscraft64.z64

-include $(wildcard $(BUILD_DIR)/src/*.d)

.PHONY: all clean
