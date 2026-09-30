# TOWNSCRAFT build.  Requires gcc with 32-bit (-m32) support, binutils and python3.
CC      = gcc
LD      = ld
OBJCOPY = objcopy
PYTHON  = python3

BUILD   = build
CFLAGS  = $(EXTRA) -g -m32 -march=i386 -mtune=i386 -O2 -ffreestanding -fno-pic -fno-pie \
          -fno-stack-protector -fno-asynchronous-unwind-tables -fno-builtin \
          -mno-80387 -mno-mmx -mno-sse -mgeneral-regs-only -fno-tree-loop-distribute-patterns \
          -Wall -Wno-unused-function -std=gnu99
ASFLAGS = -m32

SRCS    = $(wildcard src/*.c)
OBJS    = $(BUILD)/crt0.o $(BUILD)/isr.o $(BUILD)/span.o $(BUILD)/tables.o $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))

all: TOWNSCRAFT.ISO

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/crt0.o: src/crt0.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/kernel.elf: $(OBJS) src/kernel.ld
	$(LD) -m elf_i386 --no-warn-rwx-segments -T src/kernel.ld -o $@ $(OBJS)

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary -j .text -j .rodata -j .data $< $@

$(BUILD)/ipl.bin: boot/ipl.S | $(BUILD)
	$(CC) -m32 -c $< -o $(BUILD)/ipl.o
	$(LD) -m elf_i386 -Ttext=0 --oformat binary -o $@ $(BUILD)/ipl.o

TOWNSCRAFT.ISO: $(BUILD)/ipl.bin $(BUILD)/kernel.bin tools/mkimage.py
	$(PYTHON) tools/mkimage.py $(BUILD)/ipl.bin $(BUILD)/kernel.bin $@

# Test-only stub ROM set (see tools/stubrom/stubrom.S)
stubrom: $(BUILD)/stubrom.bin
	$(PYTHON) tools/stubrom/mkstubrom.py $< $(BUILD)/STUBROM

$(BUILD)/stubrom.bin: tools/stubrom/stubrom.S | $(BUILD)
	$(CC) -m32 -c $< -o $(BUILD)/stubrom.o
	$(LD) -m elf_i386 -Ttext=0 --oformat binary -o $@ $(BUILD)/stubrom.o

clean:
	rm -rf $(BUILD) TOWNSCRAFT.ISO

.PHONY: all clean stubrom

$(BUILD)/isr.o: src/isr.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/tables.c: tools/gentables.py | $(BUILD)
	$(PYTHON) tools/gentables.py $@

$(BUILD)/tables.o: $(BUILD)/tables.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/span.o: src/span.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@
