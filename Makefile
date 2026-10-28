TARGET  := x86_64-elf

# prefer the cross compiler, but plain host gcc builds the exact same kernel as
# long as we stay -ffreestanding -nostdlib
CC      := $(shell command -v $(TARGET)-gcc >/dev/null 2>&1 && echo $(TARGET)-gcc || echo gcc)
LD      := $(CC)

CFLAGS  := -std=gnu11 -ffreestanding -O2 -g -Wall -Wextra -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
           -Ikernel/include -MMD -MP
LDFLAGS := -ffreestanding -O2 -nostdlib -lgcc

BUILD   := build
KERNEL  := $(BUILD)/sparks.kernel
ISO     := sparks.iso

SRCS    := $(wildcard kernel/boot/*.S) $(wildcard kernel/src/*.c)
OBJS    := $(patsubst kernel/%,$(BUILD)/%,$(addsuffix .o,$(basename $(SRCS))))

QEMU    := qemu-system-x86_64 -cdrom $(ISO) -serial stdio

.PHONY: all iso run boot-test debug clean

all: $(KERNEL)

$(BUILD)/%.o: kernel/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: kernel/%.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(OBJS) linker.ld
	$(LD) -T linker.ld $(LDFLAGS) $(OBJS) -o $@

iso: $(KERNEL)
	mkdir -p iso/boot/grub
	cp $(KERNEL) iso/boot/sparks.kernel
	cp grub.cfg iso/boot/grub/grub.cfg
	grub2-mkrescue -o $(ISO) iso

run: iso
	$(QEMU)

# headless boot, serial log on stdout. quickest way to check we still reach the shell
boot-test: iso
	timeout 20 $(QEMU) -display none -no-reboot

debug: iso
	$(QEMU) -s -S

clean:
	rm -rf $(BUILD) $(ISO) iso

-include $(OBJS:.o=.d)
