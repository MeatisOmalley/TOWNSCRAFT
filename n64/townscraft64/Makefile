# Build with tools/build.py, which sets N64_INST to the pinned toolchain in
# build/deps and runs this from a path without spaces.
BUILD_DIR = build/rom
include $(N64_INST)/include/n64.mk

SRC = src/main.c

all: townscraft64.z64

$(BUILD_DIR)/townscraft64.elf: $(SRC:%.c=$(BUILD_DIR)/%.o)

townscraft64.z64: N64_ROM_TITLE = "Townscraft64"

# Towns core, staged by tools/stage_core.py. Built with the Towns warning set
# (warnings are not errors there) plus -Wcast-align=strict, which reports
# casts that can trap on MIPS. raster.c is the x86 software rasterizer, which
# the RDP replaces.
CORE_DIR = build/core/src
CORE_SRC = $(filter-out $(CORE_DIR)/raster.c,$(wildcard $(CORE_DIR)/*.c))
CORE_OBJ = $(CORE_SRC:$(CORE_DIR)/%.c=$(BUILD_DIR)/core/%.o)
CORE_CFLAGS = $(N64_CFLAGS) -std=gnu99 -O2 -Wall -Wextra -Wno-unused-function \
	-Wno-error -Wcast-align=strict -Werror=implicit-function-declaration

core: $(CORE_OBJ)

$(BUILD_DIR)/core/%.o: $(CORE_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "    [CORE] $<"
	$(N64_CC) -c $(CORE_CFLAGS) -MMD -o $@ $<

# Towns host tests as N64 ROMs (big-endian, real VR4300), run by
# tools/run_tests.py. Only tests of code the N64 runs unchanged from DOScraft:
# player physics and the mob manager/codec. Not built: world generation and
# the Towns 256-world cache/stream/edit tests (DOScraft ships a prebaked 1024
# world through its large-world overlays, generated on the PC), keyboard,
# save/HDD and render pixel tests (all replaced on the N64).
TEST_NAMES = player_test mobs_test mob_regions_test
TEST_CORE_DEFAULT = blocks fmath libc tables
TEST_CORE_player_test = player physics blocks fmath libc tables

$(BUILD_DIR)/tests/harness.o: tests/harness.c
	@mkdir -p $(dir $@)
	@echo "    [CC] $<"
	$(N64_CC) -c $(N64_CFLAGS) -MMD -o $@ $<

$(BUILD_DIR)/tests/%.o: build/core/tests/%.c
	@mkdir -p $(dir $@)
	@echo "    [TEST] $<"
	$(N64_CC) -c $(CORE_CFLAGS) -I$(CORE_DIR) -MMD -o $@ $<

# n64.mk's "%.z64: $(BUILD_DIR)/%.elf" keeps the target's directory, so the
# ELF for build/tests/X.z64 is build/tests/$(BUILD_DIR)/X.elf.
define TEST_template
build/tests/$(BUILD_DIR)/$(1).elf: $(BUILD_DIR)/tests/$(1).o $(BUILD_DIR)/tests/harness.o \
	$(addprefix $(BUILD_DIR)/core/,$(addsuffix .o,$(or $(TEST_CORE_$(1)),$(TEST_CORE_DEFAULT))))
build/tests/$(1).z64: N64_ROM_TITLE = "T64 $(1)"
build/tests/$(1).z64: LDFLAGS += --wrap=main --wrap=exit
build/tests/$(1).z64: | build/tests $(BUILD_DIR)/build/tests
endef
$(foreach t,$(TEST_NAMES),$(eval $(call TEST_template,$(t))))

# n64.mk writes $(BUILD_DIR)/<rom>.tmp while building each ROM.
build/tests $(BUILD_DIR)/build/tests:
	mkdir -p $@

tests: $(TEST_NAMES:%=build/tests/%.z64)

clean:
	rm -rf $(BUILD_DIR) townscraft64.z64 build/tests

-include $(wildcard $(BUILD_DIR)/src/*.d $(BUILD_DIR)/core/*.d $(BUILD_DIR)/tests/*.d)

.PHONY: all clean core tests
