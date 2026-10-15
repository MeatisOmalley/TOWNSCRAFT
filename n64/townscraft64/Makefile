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

# DOScraft's shipped game source (Towns core + overlays), staged by
# tools/stage_doscraft.py, with the defines DOScraft compiles it with. As in
# DOScraft, platform headers come from vendor/doscraft/src/platform/dos.
# raster.c/trap.S are the x86 rasterizer; abi_check.c is a DOS ABI probe.
-include build/doscraft/defines.mk
DOSCRAFT_DIR = build/doscraft/imported/src
DOSCRAFT_SRC = $(filter-out $(DOSCRAFT_DIR)/raster.c $(DOSCRAFT_DIR)/abi_check.c,\
	$(wildcard $(DOSCRAFT_DIR)/*.c))
DOSCRAFT_OBJ = $(DOSCRAFT_SRC:$(DOSCRAFT_DIR)/%.c=$(BUILD_DIR)/doscraft/%.o)
DOSCRAFT_CFLAGS = $(CORE_CFLAGS) $(DOSCRAFT_DEFINES) -I$(DOSCRAFT_DIR) \
	-Ivendor/doscraft/src/platform/dos

doscraft: $(DOSCRAFT_OBJ)

$(BUILD_DIR)/doscraft/%.o: $(DOSCRAFT_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "    [DOSCRAFT] $<"
	$(N64_CC) -c $(DOSCRAFT_CFLAGS) -MMD -o $@ $<

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

# DOScraft's native tests (tools/stage_doscraft_tests.py writes tests.mk:
# sources, include dirs and an optional case argument per ROM).
-include build/dtests/tests.mk
DTEST_CFLAGS = $(CORE_CFLAGS)

define DTEST_template
$(BUILD_DIR)/dtests/$(1)/harness.o: tests/harness.c
	@mkdir -p $$(dir $$@)
	$(N64_CC) -c $(N64_CFLAGS) $(if $(DTEST_ARG_$(1)),-DTEST_ARG='"$(DTEST_ARG_$(1))"') -o $$@ $$<
$(BUILD_DIR)/dtests/$(1)/%.o: build/dtests/%.c
	@mkdir -p $$(dir $$@)
	@echo "    [DTEST] $$<"
	$(N64_CC) -c $(DTEST_CFLAGS) $(DTEST_INC_$(1)) -MMD -o $$@ $$<
build/dtests/$(BUILD_DIR)/$(1).elf: $(BUILD_DIR)/dtests/$(1)/harness.o \
	$(patsubst build/dtests/%.c,$(BUILD_DIR)/dtests/$(1)/%.o,$(DTEST_SRC_$(1)))
build/dtests/$(1).z64: N64_ROM_TITLE = "T64 $(1)"
build/dtests/$(1).z64: LDFLAGS += --wrap=main --wrap=exit
build/dtests/$(1).z64: | $(BUILD_DIR)/build/dtests
endef
$(foreach t,$(DTEST_NAMES),$(eval $(call DTEST_template,$(t))))

$(BUILD_DIR)/build/dtests:
	mkdir -p $@

dtests: $(DTEST_NAMES:%=build/dtests/%.z64)

clean:
	rm -rf $(BUILD_DIR) townscraft64.z64 build/tests build/dtests/*.z64

-include $(wildcard $(BUILD_DIR)/src/*.d $(BUILD_DIR)/core/*.d $(BUILD_DIR)/doscraft/*.d $(BUILD_DIR)/tests/*.d)

.PHONY: all clean core doscraft tests dtests
