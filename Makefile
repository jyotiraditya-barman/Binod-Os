AS = i386-elf-as
CC = i386-elf-gcc
LD = i386-elf-ld

SRCDIR := src
OBJDIR := obj
OUTDIR := output

ISO_DIR := $(OUTDIR)/iso
ISO_BOOT := $(ISO_DIR)/boot
ISO_GRUB := $(ISO_BOOT)/grub

CFLAGS = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -I$(SRCDIR)
LDFLAGS = -m elf_i386

# Explicit kernel source list (exclude host-side utilities like mkfs, fs_tool, put)
KERNEL_C := kernel.c ata.c fs.c io.c kstring.c interrupt.c vga_mode13.c bmp.c
KERNEL_S := boot.s isr80.s

SRCS := $(addprefix $(SRCDIR)/,$(KERNEL_C))
ASMS := $(addprefix $(SRCDIR)/,$(KERNEL_S))
OBJS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS)) $(patsubst $(SRCDIR)/%.s,$(OBJDIR)/%.o,$(ASMS))

# Linker script is at project root
LINKER := linker.ld

all: $(OUTDIR)/myos.bin

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OUTDIR):
	mkdir -p $(OUTDIR)

# compile C sources
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# assemble .s sources
$(OBJDIR)/%.o: $(SRCDIR)/%.s | $(OBJDIR)
	$(AS) --32 $< -o $@

$(OUTDIR)/myos.elf: $(OBJS) $(LINKER) | $(OUTDIR)
	$(LD) $(LDFLAGS) -T $(LINKER) -o $@ $(OBJS)

$(OUTDIR)/myos.bin: $(OUTDIR)/myos.elf
	objcopy -O binary $< $@ || true

run: $(OUTDIR)/myos.elf
	# run qemu with disk image as hda (make sure disk.img exists)
	qemu-system-i386 -drive file=disk.img,format=raw -kernel $< -serial stdio

clean:
	rm -rf $(OBJDIR)/* $(OUTDIR)/*bmp.c vga_mode13.c

.PHONY: all clean run

# Build a user ELF (raycaster) that the kernel can run via fs_run
output/user_ray.elf: $(SRCDIR)/user_ray.c | $(OUTDIR)
	$(CC) $(CFLAGS) -nostdlib -nostartfiles -Wl,-e,entry -o $@ $<

.PHONY: user_ray
user_ray: output/user_ray.elf

# Build a bootable ISO using grub-mkrescue (if available).
iso: $(OUTDIR)/myos.elf | $(OUTDIR)
	@mkdir -p $(ISO_GRUB)
	@cp $< $(ISO_BOOT)/myos.elf
	@cp $(SRCDIR)/grub.cfg $(ISO_GRUB)/grub.cfg
	@if command -v grub-mkrescue >/dev/null 2>&1; then \
		grub-mkrescue -o $(OUTDIR)/myos.iso $(ISO_DIR); \
		echo "Created $(OUTDIR)/myos.iso"; \
	else \
		echo "grub-mkrescue not found. To create ISO manually run:"; \
		echo "  grub-mkrescue -o $(OUTDIR)/myos.iso $(ISO_DIR)"; \
	fi
