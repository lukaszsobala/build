# Sophgo SG2000 Bootloader Blobs for Milkv Duo S

This directory should contain the `fip.bin` bootloader blob files for the Milkv Duo S board.

## Required Files

- `fip.bin` - Combined FSBL + OpenSBI + U-Boot firmware image

## How to Obtain

The `fip.bin` can be obtained from:

### Option 1: Extract from Official Sophgo SDK Images

Download the official Milkv Duo S SD card images from:
https://github.com/milkv-duo/duo-buildroot-sdk-v2/releases

#### For RISC-V (from Fishwaldo's image):

1. Download `duos_sd.img` from https://github.com/Fishwaldo/sophgo-sg200x-debian/releases

2. Mount the boot partition and extract fip.bin:
   ```bash
   sudo mkdir -p /tmp/duos-boot
   sudo mount -o loop,ro,offset=512 duos_sd.img /tmp/duos-boot
   cp /tmp/duos-boot/fip.bin fip-riscv.bin
   sudo umount /tmp/duos-boot
   ```

#### For ARM64 (from Sophgo SDK v2):

1. Download `milkv-duos-glibc-arm64-sd_v2.0.1.img` (or similar) from the SDK releases

2. Mount the boot partition (starts at sector 1, offset 512) and extract fip.bin:
   ```bash
   sudo mkdir -p /tmp/duos-arm64-boot
   sudo mount -o loop,ro,offset=512 milkv-duos-glibc-arm64-sd_v2.0.1.img /tmp/duos-arm64-boot
   cp /tmp/duos-arm64-boot/fip.bin fip-arm64.bin
   sudo umount /tmp/duos-arm64-boot
   ```

3. Verify the extracted blob:
   ```bash
   file fip-arm64.bin        # Should show "data"
   xxd -l 16 fip-arm64.bin   # Should start with "CVBL01"
   ```

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

**IMPORTANT:** The Milkv Duo S requires **different** `fip.bin` files for ARM64 and RISC-V modes.
Each blob must be built specifically for the target architecture with the correct board identifier.

The FSBL contains a board identifier (e.g., "Milk-V DuoS") that configures:
- DDR initialization parameters
- SD/eMMC interface settings
- Clock frequencies and hardware initialization

If the board identifier is missing or incorrect, boot will fail with errors like:
```
Mount SD failed (13)
eMMC initializing failed
Boot failed (8)
```

### Required Files

- `fip-arm64.bin` - Built for ARM64 (Cortex-A53) mode
- `fip-riscv.bin` - Built for RISC-V (C906) mode

### Verification

Verify the blobs have the correct format:
```bash
# Check header (should show "CVBL01")
xxd -l 16 fip-arm64.bin
xxd -l 16 fip-riscv.bin

# Check for board identifier (RISC-V blob should have it)
strings fip-riscv.bin | grep "Milk-V DuoS"
```

**Note:** The ARM64 blob from Sophgo SDK v2.x may not contain the "Milk-V DuoS" string,
but should still work as it's from the official Duo S ARM64 image.

The build system will automatically use the appropriate file based on the target architecture.

## Boot Chain

The boot sequence is:
1. BootROM (built into SoC)
2. FSBL (First Stage Boot Loader) - initializes DDR, loads OpenSBI + U-Boot
3. OpenSBI (RISC-V only) - provides supervisor binary interface
4. U-Boot - loads kernel via extlinux.conf
5. Linux kernel

The `fip.bin` contains FSBL, OpenSBI (for RISC-V), and U-Boot combined.
