# Binod OS (myos)

Small hobby x86 (32-bit) operating system project implemented in C and assembly.

This repository contains a kernel, some user programs, and small host-side utilities to build a filesystem and image for testing. It targets i386 (32-bit) and is intended for learning, experimenting, and demos (graphics, tiny filesystem, simple games).

## Highlights

- 32-bit x86 kernel written in C and assembly
- Simple filesystem tools (mkfs, fs_tool, tinyfs code in `src/fs.c`)
- VGA Mode 13 / framebuffer demos and a Tetris demo
- Small user program support (see `output/user_ray.elf` target)
- Build and run using the provided `Makefile`

## Prerequisites

Install a cross-toolchain for 32-bit i386 (or ensure the tools below are available):

- `i386-elf-gcc` (cross-compiler)
- `i386-elf-as`
- `i386-elf-ld`
- `grub-mkrescue` (optional, to build an ISO)
- `qemu-system-i386` (optional, to run the kernel locally)

On Debian/Ubuntu you can often install packages like `qemu-system-x86` and build a cross-toolchain or use a prebuilt one (not covered here).

## Quick build & run

The `Makefile` in the project root contains the canonical build and run targets. The most common flow is:

```bash
# Build kernel and outputs
make

# Build a user ELF (example program)
make user_ray

# Run with qemu (assumes disk.img exists)
make run
# or directly
qemu-system-i386 -drive file=disk.img,format=raw -kernel output/myos.elf -serial stdio

# Create a bootable ISO (requires grub-mkrescue)
make iso
# If grub-mkrescue is not installed, the Makefile prints a hint how to create the ISO manually
```

Notes:
- `make` builds `output/myos.elf` and `output/myos.bin`.
- `make run` runs `qemu-system-i386` using `disk.img` as the drive (you must create/provide `disk.img` or modify the command).

## Project layout

Top-level files/directories (short description):

- `Makefile` — build rules (compiles sources in `src/`, links with `linker.ld`).
- `linker.ld` — linker script for the kernel image.
- `src/` — kernel and utility sources (C and assembly).
  - `kernel.c`, `boot.s`, `isr80.s`, `interrupt.c` — kernel core and startup.
  - `fs.c`, `mkfs.c`, `fs_tool.c`, `put.c` — filesystem and host-side helpers.
  - `vga_mode13.c`, `framebuffer.c`, `tetris.c` — graphics and demo code.
  - `user_ray.c` — example user-space program target (`make user_ray` builds `output/user_ray.elf`).
- `mkfs`, `fs_tool`, `put` — host-side utilities (some live at repo root and in `src/` as well).
- `debug/` — debugging helpers and experiments.
- `obj/`, `output/` — build outputs and object files.

## How it fits together

- The kernel sources in `src/` are compiled for i386 and linked using `linker.ld` to produce `output/myos.elf`.
- The project includes small host-side utilities to create a disk image and manipulate files that the kernel can read.
- You can run the kernel directly with QEMU using the `-kernel` option (as above), or build an ISO with GRUB and boot it.

## Development notes & tips

- If you don't have a cross-toolchain, you can install `gcc-multilib` and try to adapt the `Makefile` flags, but the project expects i386-elf tools.
- The Makefile `run` target assumes `disk.img` exists. To make a quick raw disk image, you can do something like (example):

```bash
# Create a 64MB empty disk image
dd if=/dev/zero of=disk.img bs=1M count=64
# Format and populate the image with your filesystem using mkfs / fs_tool (project-specific)
# (Adjust the commands to what the repo utilities expect.)
```

- Use `make iso` if you prefer a bootable ISO. If `grub-mkrescue` is not installed the Makefile will print a hint.

## Contributing

Contributions, experiments, and small improvements are welcome. A few suggestions:

- Add a `LICENSE` file if you want to set a license (MIT/Apache/etc.).
- Add a small `docs/` folder describing the filesystem format and the boot process if you want to attract contributors.
- Add small tests or emulation harnesses for host-side tools to validate `mkfs` and `fs_tool` behavior.

## License
none make anything and make edit it i dont care.

