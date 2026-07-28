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

> **ARM mode no longer uses this directory.** `milkv-duos-arm` builds its own
> `fip.bin` from source on every build — see
> `config/sources/families/include/sophgo-sg200x_uboot.inc` and
> `packages/sophgo-sg200x/u-boot/`. The blobs below are only for the RISC-V
> family, which has not been converted yet.

## Blobs shipped here

| File | Mode | CPU |
| --- | --- | --- |
| `fip-riscv64.bin` | RISC-V (C906) | 850 MHz (vendor default) |
| `fip-riscv64-od.bin` | RISC-V (C906) | 1050 MHz (overdrive) |

Build with `SOPHGO_CPU_OVERDRIVE=yes` to select the overdrive variant.

Both come from [queenkjuul/milkv-duo-ubuntu][qkj] (`milkv-bootloader/duos/`),
built from `duo-buildroot-sdk-v2` with the patches in `bootloader-patches/`.

**There is no ARM blob here, and there does not need to be.** Nobody publishes a
distroboot-enabled ARM `fip.bin` for the Duo S, so rather than commit one,
`milkv-duos-arm` builds it during the normal Armbian u-boot artifact build. The
stock ARM `fip.bin` from the Milk-V SDK images would not work anyway: it boots a
fixed FIT image (`boot.sd`) rather than scanning for `/extlinux/extlinux.conf`.

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

## Rebuilding a RISC-V blob by hand

Only needed while the RISC-V family still consumes prebuilt blobs. The
supported route is the one ARM already uses — see
`config/sources/families/include/sophgo-sg200x_uboot.inc` — which needs an
OpenSBI step added for RISC-V.

```bash
git clone --depth 1 https://github.com/milkv-duo/duo-buildroot-sdk-v2.git
cd duo-buildroot-sdk-v2
git am /path/to/armbian/packages/blobs/sophgo/milkv-duos/bootloader-patches/*.patch
source build/envsetup_soc.sh
defconfig sg2000_milkv_duos_musl_riscv64_sd
build_fsbl
```

The result lands in `install/soc_<board>/fip.bin`; drop it in here as
`fip-riscv64.bin`.

Two things break with a modern toolchain, both worked around in the Armbian
build and needed here too:

- gcc >= 13 rejects U-Boot 2021.10's `common/command.c` — pass
  `KCFLAGS=-Wno-error=enum-int-mismatch`.
- binutils >= 2.39 warns about the FSBL's RWX `LOAD` segment, and the FSBL links
  with `--fatal-warnings` — pass `LDFLAGS=--no-warn-rwx-segments`.

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
  (`github.com/sophgo/fsbl`); the only prebuilt pieces are the ARM EL3 monitor
  `bl31.bin` and `bl32.bin`, ~45 KiB together, which ship inside that repo at
  `plat/cv181x/prebuilt/`.

[qkj]: https://github.com/queenkjuul/milkv-duo-ubuntu
