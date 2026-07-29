# Sophgo SG200x runtime blobs (Milk-V Duo S)

Nothing is left here. Both things this directory used to hold — the bootloader
and the Wi-Fi firmware — now come from somewhere better, and this note says
where, so the next person to go looking does not conclude they were lost.

## Wi-Fi/Bluetooth firmware — now `armbian-firmware`

`aic8800-firmware/` held Milk-V's aicsemi build for the AIC8800D80 Wi-Fi 6 + BT 5
chip on the Duo S, taken from `milkv-duo/duo-buildroot-sdk-v2`. The
`sophgo-aic8800` extension copied it into the rootfs and compiled the driver to
look there.

`armbian-firmware` — installed in every image — already ships an AIC8800D80 SDIO
set at `/lib/firmware/aic8800/SDIO/aic8800D80`, which is what the other AIC8800
boards in this repo use. It is a *different* aicsemi build to Milk-V's: of the
seven files, only `fw_adid_8800d80_u02.bin` was byte-identical, and the
host/firmware message ABI (`lmac_msg.h`) generally pairs with a driver revision,
so the two were not assumed interchangeable. It was tested instead, on a Duo S
running the pinned driver, by pointing the module at the other directory:

```sh
modprobe aic8800_bsp aic_fw_path=/lib/firmware/aic8800/SDIO/aic8800D80
```

Firmware upload, BT patch table, phy registration with HE capability, 5 GHz
association and iperf3 all came up clean, so the local copy was dropped. The
extension now compiles the driver to point at the shared path and asserts at
build time that the files are present — see `extensions/sophgo-aic8800.sh`.

Two incidental wins: the `AICWFDBG(LOGERROR) invalid cmd: lvl_adj_5g_chan_*`
noise on boot is gone, because those keys existed only in Milk-V's
`aic_userconfig_8800d80.txt` and this firmware build rejects them; and
`fmacfwbt_8800d80_u02.bin` is no longer carried at all — the driver only names
the Wi-Fi+BT combo image when built with `CONFIG_SDIO_BT=y`, and the vendor
Makefile sets `n` because Bluetooth here is a UART HCI on `uart4`.

The one file with no upstream equivalent in normal use is `lmacfw_rf_*`, and
`armbian-firmware` ships that too. If a future firmware bump upstream ever breaks
the Duo S, the fix is the same command above pointed at a directory holding
Milk-V's build again; `git log` has the blobs.

## Bootloader — now built from source

A prebuilt `fip.bin` per architecture used to live here, with
`fip-riscv64.bin`, `fip-riscv64-od.bin` and `bootloader-patches/`. Both
`milkv-duos-arm` and `milkv-duos-riscv` now build theirs on every build, so the
blobs and the `sophgo-fip-blobs` extension that installed them are gone. See
`config/sources/families/include/sophgo-sg200x_uboot.inc` and
`packages/sophgo-sg200x/u-boot/`.

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
