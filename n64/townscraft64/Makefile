# Build with tools/build.py, which sets N64_INST to the pinned toolchain in
# build/deps and runs this from a path without spaces.
BUILD_DIR = build/rom
include $(N64_INST)/include/n64.mk

all: townscraft64.z64

# N1 bring-up ROM: spinning triangle, RAM size, controller-port report.
hello: build/hello.z64
build/$(BUILD_DIR)/hello.elf: $(BUILD_DIR)/src/hello/main.o
build/hello.z64: N64_ROM_TITLE = "T64 hello"

# The game: DOScraft's N64 staging (tools/stage_doscraft.py n64), DOScraft's
# portable platform files, and the N64 platform layer in src/n64.
-include build/n64game/defines.mk
GAME_DIR = build/n64game/imported/src
GAME_PLATFORM_DIR = build/n64game/platform/src
DOS_DIR = vendor/doscraft/src/platform/dos
GAME_CORE = blocks fmath font game inventory libc mobs physics player raster render tables 	textures ui world
GAME_DOS = heap gfx keyboard craft_grid viewmodel graphics_presets sound_silent
GAME_N64 = $(wildcard src/n64/*.c)
GAME_OBJ = $(GAME_CORE:%=$(BUILD_DIR)/game/core/%.o) $(GAME_DOS:%=$(BUILD_DIR)/game/dos/%.o) 	$(GAME_N64:src/n64/%.c=$(BUILD_DIR)/game/n64/%.o)
GAME_WARN = -std=gnu99 -O2 -Wall -Wextra -Wno-unused-function -Wno-error 	-Wcast-align=strict -Werror=implicit-function-declaration
# As build_game: core files see the platform headers first; platform files see
# the staged Towns headers (with DOScraft's chicken changes) first.
GAME_CORE_FLAGS = $(N64_CFLAGS) $(GAME_WARN) $(N64GAME_DEFINES) -I$(DOS_DIR) -I$(GAME_DIR) -Isrc/n64
GAME_PLATFORM_FLAGS = $(N64_CFLAGS) $(GAME_WARN) $(N64GAME_PLATFORM_DEFINES) -I$(GAME_PLATFORM_DIR) 	-I$(DOS_DIR) -Isrc/n64

$(BUILD_DIR)/game/core/%.o: $(GAME_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "    [GAME] $<"
	$(N64_CC) -c $(GAME_CORE_FLAGS) -MMD -o $@ $<
$(BUILD_DIR)/game/dos/%.o: $(DOS_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "    [GAME] $<"
	$(N64_CC) -c $(GAME_PLATFORM_FLAGS) -MMD -o $@ $<
$(BUILD_DIR)/game/n64/%.o: src/n64/%.c
	@mkdir -p $(dir $@)
	@echo "    [N64] $<"
	$(N64_CC) -c $(GAME_PLATFORM_FLAGS) -MMD -o $@ $<

$(BUILD_DIR)/townscraft64.elf: $(GAME_OBJ)
townscraft64.z64: N64_ROM_TITLE = "Townscraft64"
townscraft64.z64: N64_ROM_SAVETYPE = flashram
# The ROM's files: the 1024-world medium (build/romfs/terrain.bin, staged by
# tools/stage_romfs.py from the pinned compact world).
townscraft64.z64: $(BUILD_DIR)/townscraft64.dfs
$(BUILD_DIR)/townscraft64.dfs: N64_MKDFS_ROOT = build/romfs
$(BUILD_DIR)/townscraft64.dfs: $(wildcard build/romfs/*)

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
$(BUILD_DIR)/dtests/$(1)/%.o: %.c
	@mkdir -p $$(dir $$@)
	@echo "    [DTEST] $$<"
	$(N64_CC) -c $(DTEST_CFLAGS) $(DTEST_INC_$(1)) -MMD -o $$@ $$<
build/dtests/$(BUILD_DIR)/$(1).elf: $(BUILD_DIR)/dtests/$(1)/harness.o \
	$(patsubst %.c,$(BUILD_DIR)/dtests/$(1)/%.o,$(DTEST_SRC_$(1)))
build/dtests/$(1).z64: N64_ROM_TITLE = "T64 $(1)"
build/dtests/$(1).z64: LDFLAGS += --wrap=main --wrap=exit
build/dtests/$(1).z64: | $(BUILD_DIR)/build/dtests
endef
$(foreach t,$(DTEST_NAMES),$(eval $(call DTEST_template,$(t))))

# ROMs that carry files (the 1024-world medium) get a DragonFS image.
define DTEST_DFS_template
build/dtests/$(1).z64: $(BUILD_DIR)/dtests/$(1).dfs
$(BUILD_DIR)/dtests/$(1).dfs: N64_MKDFS_ROOT = $(DTEST_DFS_$(1))
$(BUILD_DIR)/dtests/$(1).dfs: $(wildcard $(DTEST_DFS_$(1))/*)
endef
$(foreach t,$(DTEST_DFS_NAMES),$(eval $(call DTEST_DFS_template,$(t))))

$(BUILD_DIR)/build/dtests:
	mkdir -p $@

dtests: $(DTEST_NAMES:%=build/dtests/%.z64)

clean:
	rm -rf $(BUILD_DIR) townscraft64.z64 build/tests build/dtests/*.z64

-include $(wildcard $(BUILD_DIR)/src/*.d $(BUILD_DIR)/core/*.d $(BUILD_DIR)/doscraft/*.d $(BUILD_DIR)/tests/*.d $(BUILD_DIR)/game/*/*.d)

.PHONY: all hello clean core doscraft tests dtests
