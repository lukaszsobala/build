# Sophgo SG200x bootloader blobs (Milk-V Duo S)

`fip.bin` is the single file the SG2000 BootROM knows how to load. It bundles:

1. **FSBL** — brings up DDR and the SD interface, and decides *which core runs
   Linux*: the C906 RISC-V core or the Cortex-A53. This is why RISC-V and ARM
   mode need different blobs, and why there is no runtime switch on the board.
2. **OpenSBI** (RISC-V only).
3. **U-Boot 2021.10**, from the Sophgo/CVITEK vendor fork.

The BootROM looks for a file literally named `fip.bin` in the first FAT
partition of the card. Armbian's `sophgo-fip-blobs` extension copies the right
blob to `/boot/fip.bin` at the end of the image build.

## Blobs shipped here

| File | Mode | CPU |
| --- | --- | --- |
| `fip-riscv64.bin` | RISC-V (C906) | 850 MHz (vendor default) |
| `fip-riscv64-od.bin` | RISC-V (C906) | 1050 MHz (overdrive) |

Build with `SOPHGO_CPU_OVERDRIVE=yes` to select the overdrive variant.

Both come from [queenkjuul/milkv-duo-ubuntu][qkj] (`milkv-bootloader/duos/`),
built from `duo-buildroot-sdk-v2` with the patches in `bootloader-patches/`.

**There is no ARM blob here.** Nobody publishes a distroboot-enabled ARM
`fip.bin` for the Duo S, so `milkv-duos-arm` images are currently written
*without* a bootloader unless you build one — see below. The stock ARM
`fip.bin` from the Milk-V SDK images will *not* work: it boots a fixed FIT
image (`boot.sd`) rather than scanning for `/extlinux/extlinux.conf`.

## Why distroboot matters

The stock vendor U-Boot runs `run sdboot`, which `fatload`s a single monolithic
FIT image from the card and boots it. That makes kernel upgrades through `apt`
impossible without regenerating the FIT.

`bootloader-patches/0001-u-boot-cvi181x-enable-distroboot-for-cvi181x.patch`
switches `CONFIG_BOOTCOMMAND` to `run distro_bootcmd || run sdboot` and defines
the `*_addr_r` load addresses distroboot needs (notably `fdt_addr_r=0x82200000`,
away from where the kernel decompresses). U-Boot then walks the partitions
marked bootable in the MBR and parses `/extlinux/extlinux.conf`, which is
exactly what Armbian writes with `SRC_EXTLINUX=yes`.

## Building a blob yourself

Needed for ARM mode; also the route to rebuilding the RISC-V blob from source.

1. Clone the vendor SDK and its host tools:

   ```bash
   git clone https://github.com/milkv-duo/duo-buildroot-sdk-v2.git
   cd duo-buildroot-sdk-v2
   git clone https://github.com/milkv-duo/host-tools.git
   ```

2. Apply the patches from `bootloader-patches/`:

   ```bash
   git am /path/to/armbian/packages/blobs/sophgo/milkv-duos/bootloader-patches/*.patch
   ```

   `0001` and `0004` touch `u-boot-2021.10/include/configs/cv181x-asic.h` and are
   shared by the ARM and RISC-V builds of the CV181x/SG2000 family. `0002` (CPU
   overdrive) is applied to the RISC-V board defconfig; for ARM apply the same
   `CONFIG_OD_CLK_SEL=y` line to the ARM board defconfig instead.

3. Build the FSBL for the board you want:

   ```bash
   # RISC-V
   source build/cvisetup.sh && defconfig sg2000_milkv_duos_musl_riscv64_sd && build_fsbl
   # ARM
   source build/cvisetup.sh && defconfig sg2000_milkv_duos_musl_arm64_sd && build_fsbl
   ```

4. The result lands in `install/soc_<board>/fip.bin`. Drop it in here as
   `fip-arm64.bin` or `fip-riscv64.bin`.

## Caveats

- `0005-u-boot-set-root-partition-to-mmcblk0p3.patch` hardcodes
  `root=/dev/mmcblk0p3` into the `sdboot` *fallback* path, matching the upstream
  three-partition layout. Armbian uses two partitions and passes `root=UUID=…`
  from `extlinux.conf`, so this only affects the fallback, which Armbian does not
  install anyway.
- These are redistributable binaries built from the publicly available Sophgo
  SDK; the FSBL portion is vendor code without published sources.

[qkj]: https://github.com/queenkjuul/milkv-duo-ubuntu
