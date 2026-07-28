# Sophgo SG200x runtime blobs (Milk-V Duo S)

Only Wi-Fi/Bluetooth firmware lives here now. The bootloader used to as well —
a prebuilt `fip.bin` per architecture — but both `milkv-duos-arm` and
`milkv-duos-riscv` build theirs from source on every build, so the blobs and the
`sophgo-fip-blobs` extension that installed them are gone. See
`config/sources/families/include/sophgo-sg200x_uboot.inc` and
`packages/sophgo-sg200x/u-boot/`.

## `aic8800-firmware/`

Firmware for the AIC8800D80 Wi-Fi 6 + BT 5 chip on the Duo S, which hangs off
SDIO. There is no mainline driver; the `sophgo-aic8800` extension builds the
vendor one into the kernel and copies these into `/lib/firmware/aic8800`.

| File | Loaded by |
| --- | --- |
| `fw_patch_table_8800d80_u02.bin` | `aic8800_bsp`, first — also the BT patch table |
| `fw_adid_8800d80_u02.bin` | `aic8800_bsp` |
| `fw_patch_8800d80_u02.bin` | `aic8800_bsp` |
| `fmacfw_8800d80_u02.bin` | `aic8800_fdrv`, the Wi-Fi MAC firmware |
| `fmacfwbt_8800d80_u02.bin` | Wi-Fi + BT combo firmware |
| `lmacfw_rf_8800d80_u02.bin` | RF/LMAC firmware |
| `aic_userconfig_8800d80.txt` | per-board RF calibration, parsed at load |

The driver opens these by absolute path from `CONFIG_AIC_FW_PATH` and has no
fallback. If they are missing it retries forever, power-cycling the chip about
every 1.7s — so the extension asserts that at least the patch table landed in
the rootfs rather than letting a silent miss ship. The `AICWFDBG(LOGERROR)
invalid cmd: lvl_adj_5g_chan_*` lines on boot are entries in
`aic_userconfig_8800d80.txt` that this firmware build does not accept; they are
harmless.

Source: `milkv-duo/duo-buildroot-sdk-v2`, matching the driver revision pinned in
`extensions/sophgo-aic8800.sh`. Re-sync both together.

## What is no longer here

`fip-riscv64.bin`, `fip-riscv64-od.bin` and `bootloader-patches/` were removed
once the RISC-V bootloader was booting from source (`git log` for the history).
Of that patch series, the parts Armbian still needs live in their proper places:

| Was | Now |
| --- | --- |
| 0001 u-boot distroboot | `patch/u-boot/u-boot-sophgo-sg200x/0001-…` |
| 0002 fsbl OD_CLK_SEL | `SOPHGO_CPU_OVERDRIVE=yes` → `OD_CLK_SEL=y` |
| 0003 opensbi `fence.tso` | `patch/atf/opensbi-sophgo-sg200x/0001-…` |
| 0004 u-boot `of_libfdt_overlay` | `CONFIG_OF_LIBFDT_OVERLAY=y` in both defconfigs |
| 0005 root partition `mmcblk0p3` | dropped — only affects the vendor `sdboot` fallback, which Armbian does not install |
| 0006, 0007 | dropped — sg2002 / Duo 256M, a different board |

The only binaries left in the boot chain are inside the upstream repos
themselves: `bl31.bin` and `bl32.bin` (~45 KiB, ARM only) in
`sophgo/fsbl`'s `plat/cv181x/prebuilt/`, and the 1832-byte
`pm_default_cv181x.bin` suspend stub that `sophgo/opensbi` `.incbin`s into its
cvitek platform override.
