# Sophgo SG2000 Bootloader Blobs for Milkv Duo S

This directory should contain the `fip.bin` bootloader blob files for the Milkv Duo S board.

## Required Files

- `fip.bin` - Combined FSBL + OpenSBI + U-Boot firmware image

## How to Obtain

The `fip.bin` can be obtained from:

### Option 1: Extract from Official Milkv Duo S SD Card Image

1. Download the official Milkv Duo S SD card image from:
   https://github.com/milkv-duo/duo-buildroot-sdk/releases

2. Write the image to an SD card or mount it as a loop device

3. The `fip.bin` is located in the raw sectors of the SD card (before the first partition).
   Extract it using:
   ```bash
   dd if=/dev/sdX of=fip.bin bs=512 count=4096 skip=0
   ```
   (Adjust count as needed - fip.bin is typically around 1-2MB)

### Option 2: Build from Fishwaldo's sophgo-sg200x-debian

1. Clone the repository:
   ```bash
   git clone https://github.com/Fishwaldo/sophgo-sg200x-debian.git
   ```

2. Build the fsbl package (requires Docker):
   ```bash
   podman run --privileged -it --rm \
     -v ./configs/:/configs \
     -v ./image:/output \
     ghcr.io/fishwaldo/sophgo-sg200x-debian:master \
     make BOARD=duos fsbl
   ```

3. Extract `fip.bin` from the resulting debian package or build directory

### Option 3: Download from Fishwaldo's Releases

1. Go to: https://github.com/Fishwaldo/sophgo-sg200x-debian/releases

2. Download the `duos_sd.img` release

3. Extract `fip.bin` from the raw sectors as shown in Option 1

## Architecture Note

The Milkv Duo S has a hardware switch to select between ARM64 (Cortex-A53) and
RISC-V (C906) mode. The same `fip.bin` may work for both modes as the selection
happens at the hardware level, but this needs verification.

If different fip.bin files are needed:
- Name the ARM64 version: `fip-arm64.bin`
- Name the RISC-V version: `fip-riscv.bin`

The build system will automatically use the appropriate file based on the target architecture.

## Boot Chain

The boot sequence is:
1. BootROM (built into SoC)
2. FSBL (First Stage Boot Loader) - initializes DDR, loads OpenSBI + U-Boot
3. OpenSBI (RISC-V only) - provides supervisor binary interface
4. U-Boot - loads kernel via extlinux.conf
5. Linux kernel

The `fip.bin` contains FSBL, OpenSBI (for RISC-V), and U-Boot combined.
