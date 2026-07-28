# Sophgo SG200x bootloader blobs (Milk-V Duo S)

> **Nothing in Armbian uses these any more.** Both `milkv-duos-arm` and
> `milkv-duos-riscv` build their own `fip.bin` from source on every build — see
> `config/sources/families/include/sophgo-sg200x_uboot.inc` and
> `packages/sophgo-sg200x/u-boot/`. The blobs are kept as a fallback while the
> from-source RISC-V bootloader is still new; the `sophgo-fip-blobs` extension
> only installs one when `BOOTCONFIG=none`, which no board sets today.

`fip.bin` is the single file the SG2000 BootROM knows how to load. It bundles:

1. **FSBL** — brings up DDR and the SD interface, and decides *which core runs
   Linux*: the C906 RISC-V core or the Cortex-A53. This is why RISC-V and ARM
   mode need different images, and why there is no runtime switch on the board.
   The FSBL selects the core through `TOC_HEADER_NAME`, `0xC906B001` for RISC-V
   against `0xAA640001` for aarch64.
2. **The EL3/M-mode monitor** — a prebuilt 24 KiB `bl31.bin` from inside
   `sophgo/fsbl` on ARM, OpenSBI on RISC-V.
3. **U-Boot 2021.10**, from the Sophgo/CVITEK vendor fork.

The BootROM looks for a file literally named `fip.bin` in the first FAT
partition of the card.

## Blobs shipped here

| File | Mode | CPU |
| --- | --- | --- |
| `fip-riscv64.bin` | RISC-V (C906) | 850 MHz (vendor default) |
| `fip-riscv64-od.bin` | RISC-V (C906) | 1050 MHz (overdrive) |

Both come from [queenkjuul/milkv-duo-ubuntu][qkj] (`milkv-bootloader/duos/`),
built from `duo-buildroot-sdk-v2` with the patches in `bootloader-patches/`.
`SOPHGO_CPU_OVERDRIVE=yes` selects the overdrive variant — which is also how the
from-source path picks `OD_CLK_SEL=y`, so the switch means the same thing either
way.

There has never been an ARM blob here, and there does not need to be: nobody
publishes a distroboot-enabled ARM `fip.bin` for the Duo S. The stock ARM
`fip.bin` from the Milk-V SDK images would not work anyway — it boots a fixed
FIT image (`boot.sd`) rather than scanning for `/extlinux/extlinux.conf`.

## Why distroboot matters

The stock vendor U-Boot runs `run sdboot`, which `fatload`s a single monolithic
FIT image from the card and boots it. That makes kernel upgrades through `apt`
impossible without regenerating the FIT.

`patch/u-boot/u-boot-sophgo-sg200x/0001-cvitek-cv181x-enable-distroboot.patch`
switches `CONFIG_BOOTCOMMAND` to `run distro_bootcmd || run sdboot` and defines
the `*_addr_r` load addresses distroboot needs (notably `fdt_addr_r=0x82200000`,
away from where the kernel decompresses). U-Boot then walks the partitions
marked bootable in the MBR and parses `/extlinux/extlinux.conf`, which is
exactly what Armbian writes with `SRC_EXTLINUX=yes`. It patches only
`include/configs/cv181x-asic.h`, which is shared, so one patch covers both
architectures.

## Rebuilding a blob by hand

You should not need to — `./compile.sh uboot BOARD=milkv-duos-riscv BRANCH=edge`
produces `fip.bin` inside the u-boot `.deb`. Kept for reference:

```bash
git clone --depth 1 https://github.com/milkv-duo/duo-buildroot-sdk-v2.git
cd duo-buildroot-sdk-v2
git am /path/to/armbian/packages/blobs/sophgo/milkv-duos/bootloader-patches/*.patch
source build/envsetup_soc.sh
defconfig sg2000_milkv_duos_musl_riscv64_sd
build_fsbl
```

The result lands in `install/soc_<board>/fip.bin`.

Three things break with a modern toolchain, all handled in the Armbian build and
needed here too:

- gcc >= 13 rejects U-Boot 2021.10's `common/command.c` — pass
  `KCFLAGS=-Wno-error=enum-int-mismatch`.
- binutils >= 2.39 warns about the FSBL's RWX `LOAD` segment, and the FSBL links
  with `--fatal-warnings` — pass `LDFLAGS=--no-warn-rwx-segments`.
- binutils >= 2.38 moved the CSR instructions to Zicsr and `fence.i` to Zifencei,
  which breaks U-Boot, OpenSBI and the FSBL on RISC-V in three different ways.
  See the patches under `patch/u-boot/u-boot-sophgo-sg200x/` and
  `patch/atf/fsbl-sophgo-sg200x/`, and the OpenSBI `PLATFORM_RISCV_ISA` override
  in the family include.

FreeRTOS for the companion C906L core needs `cmake`; it can be skipped entirely
with `CONFIG_ENABLE_FREERTOS=` since Linux loads that firmware via remoteproc.

## Caveats

- `0005-u-boot-set-root-partition-to-mmcblk0p3.patch` hardcodes
  `root=/dev/mmcblk0p3` into the `sdboot` *fallback* path, matching the upstream
  three-partition layout. Armbian uses two partitions and passes `root=UUID=…`
  from `extlinux.conf`, so this only affects the fallback, which Armbian does not
  install anyway. It is not carried in the Armbian-side patch set.
- These are redistributable binaries built from the publicly available Sophgo
  SDK. Contrary to what is often assumed, the FSBL *is* open source
  (`github.com/sophgo/fsbl`); the only prebuilt pieces are the ARM monitor
  `bl31.bin` and `bl32.bin` (~45 KiB together, at `plat/cv181x/prebuilt/`) and,
  on RISC-V, the 1832-byte `pm_default_cv181x.bin` suspend stub that
  `sophgo/opensbi` `.incbin`s into its cvitek platform override.

[qkj]: https://github.com/queenkjuul/milkv-duo-ubuntu
