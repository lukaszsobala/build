#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2013-2026 Igor Pecovnik, igor@armbian.com
#
# This file is a part of the Armbian Build Framework
# https://github.com/armbian/build/
#
# Everything the Sophgo SG200x ARM and RISC-V families have in common.
#
# The two families differ only in which core of the SG2000 runs Linux, and that
# is decided by the FSBL inside fip.bin - not by anything in the kernel, the
# bootloader sources or the board wiring. So almost every knob here is identical
# on both sides, and keeping two copies of it only invites drift: the U-Boot and
# FSBL commit pins in particular have to move together or the two images stop
# being the same board.
#
# Sourced near the top of both family files, before sophgo-sg200x_uboot.inc,
# which consumes SOPHGO_CVI_* to build BOOTCONFIG. What stays in the family
# files is exactly what genuinely differs; see the comments there.
#

declare -g GOVERNOR="performance"

# Bootloader: built from source on both sides.
#
# The SG200x BootROM only loads fip.bin from the FAT partition at the start of
# the card, and mainline U-Boot has no SG2000 support worth the name - none at
# all for ARM - so a vendor fip.bin is unavoidable. What we do is build every
# part of it: the vendor U-Boot 2021.10, the FSBL that packs it, and on RISC-V
# OpenSBI for the monitor slot. Details in sophgo-sg200x_uboot.inc.
declare -g BOOTSOURCE="https://github.com/sophgo/u-boot-2021.10"
declare -g BOOTDIR="u-boot-sophgo-sg200x"
declare -g BOOTBRANCH="commit:bb7496c2dfded43921695bf49c23da28381f8960" # sg200x-dev, 2026-06-30
declare -g BOOTPATCHDIR="u-boot-sophgo-sg200x"

# The FSBL consumes u-boot.bin, so it cannot be built by compile_atf() - that
# runs before u-boot. It is built from uboot_custom_postprocess() instead, and
# so is OpenSBI, which has to see u-boot's device tree.
declare -g ATFSOURCE="none"

# That vendor U-Boot is patched for distroboot, which scans the first bootable
# partition for /extlinux/extlinux.conf and then, failing that, for /boot.scr -
# scan_dev_for_boot runs scan_dev_for_extlinux before scan_dev_for_scripts. We
# take the second route, so the images carry the boot.scr and armbianEnv.txt
# every other Armbian board has. Extlinux would be less machinery, but its
# config is generated at build time with no runtime knobs: SRC_EXTLINUX and
# BOOTENV_FILE are mutually exclusive in the framework, and DT overlays could
# only be fixed at build time through DEFAULT_OVERLAYS rather than chosen by
# the user. No U-Boot change was needed for the switch - the distroboot patch
# already exports scriptaddr and BOOTENV.
# The destination has to be boot.cmd, not boot.scr. The half of the framework
# that wraps the script in a uImage keys off the destination *name*: both the
# image build (lib/functions/image/partitioning.sh) and the BSP postinst run
# mkimage only "if [[ -f /boot/boot.cmd ]]", and both hardcode boot.scr as the
# output. Name the destination boot.scr and the plain text is copied there
# under that name, mkimage never runs, and U-Boot rejects it at boot with
# 'Wrong image format for "source" command'.
declare -g BOOTSCRIPT="boot-sophgo-sg200x.cmd:boot.cmd"

# Overlays are installed by the kernel package to /boot/dtb/sophgo/overlay/ and
# named ${OVERLAY_PREFIX}-${name}.dtbo, which is the path boot.cmd builds. The
# prefix is the SoC rather than the family, because the .dtso sources live in
# arch/*/boot/dts/sophgo/overlay/ next to the board DTS they patch.
declare -g OVERLAY_PREFIX="sg2000"

# Identifies the duo-buildroot-sdk-v2 project whose board files we vendored.
# See packages/sophgo-sg200x/u-boot/README.md. The per-arch half of this set -
# SOPHGO_CVI_PROJECT, _BOARD, _FILES_VARIANT, _UBOOT_DTS_DIR, _UBOOT_ARCH and
# SOPHGO_FSBL_ARCH - lives in the family files.
declare -g SOPHGO_CVI_CHIP="sg2000"
declare -g SOPHGO_CVI_CHIP_ARCH="CV181X"
declare -g SOPHGO_CVI_DDR_CFG="ddr3_1866_x16"
declare -g SOPHGO_CVI_PANEL="MIPI_panel_milkv_8hd"

# Which medium the resulting image is meant to run from: "sd" or "emmc".
#
# This is far more than a label. u-boot's cvitek.mk turns STORAGE_TYPE into
# -DCONFIG_SD_BOOT or -DCONFIG_EMMC_SUPPORT, and cv181x-asic.h keys three things
# off that:
#
#   - CONFIG_BOOTCOMMAND. "sd" boots straight into distro_bootcmd; "emmc" runs
#     the vendor 'cvi_update' first, which is the eMMC flasher (see
#     extensions/sophgo-sg200x-emmc-installer.sh).
#   - which mmc device is which. cv181x_asic_sd.dtsi deletes the cv-emmc@4300000
#     node outright, so on "sd" the card is mmc 0; cv181x_asic_emmc.dtsi keeps
#     it, and since it sits before cv-sd@4310000 in cv181x_base.dtsi the eMMC
#     becomes mmc 0 and the card mmc 1. cvi_update hardcodes that split, and
#     distro_bootcmd only scans mmc 0.
#   - the offsets in cvipart.h/imgs.h, which are per-storage files.
#
# It is a build-time switch on an ordinary board rather than a board of its own:
#
#   ./compile.sh build BOARD=milkv-duos-arm BRANCH=edge SOPHGO_CVI_STORAGE=emmc
#
# There used to be a milkv-duos-arm-emmc and a milkv-duos-riscv-emmc whose only
# real content was this one line, which made four board files for two physical
# boards. apply_cmdline_params_to_env() runs long before the config phase, so a
# command-line assignment is in scope here; hence ${:-} rather than a plain
# assignment.
#
# One image cannot serve both, which is why this still forks the build. An
# eMMC-built bootloader runs cvi_update on every SD boot, and _storage_update()
# in cmd/cvi_update.c fatloads fip.bin off the card and writes it into the eMMC
# boot partition without asking - every Armbian image has a /fip.bin, so a plain
# card boot would silently reflash the eMMC and then return 0, which also means
# the '||' never reaches distro_bootcmd. The mmc renumbering above is the
# smaller half of the problem.
declare -g SOPHGO_CVI_STORAGE="${SOPHGO_CVI_STORAGE:-sd}"
case "${SOPHGO_CVI_STORAGE}" in
	sd) ;;
	# Enabled here rather than from a board file so that the storage type is the
	# only knob. Extensions cannot do this the other way round: the extension
	# manager initializes after this include is sourced, so an extension hook
	# could not set SOPHGO_CVI_STORAGE in time - while enable_extension() at this
	# point is exactly what board files do.
	emmc) enable_extension "sophgo-sg200x-emmc-installer" ;;
	*) exit_with_error "SOPHGO_CVI_STORAGE must be 'sd' or 'emmc'" "${SOPHGO_CVI_STORAGE}" ;;
esac

# Kernel: mainline plus the out-of-tree Sophgo patch series.
#
# One series for both architectures. arch/arm64/boot/dts/sophgo/sg2000.dtsi
# #includes cv180x.dtsi and cv181x.dtsi straight out of arch/riscv, redefining
# SOC_PERIPHERAL_IRQ() to emit GIC_SPI specifiers, so the entire SoC description
# is shared and so are the patches that touch it. The handful of arm64-only DTS
# patches in there are inert on a RISC-V build, which never descends into
# arch/arm64. See patch/kernel/sophgo-sg200x-common/README.md.
case "${BRANCH}" in

	edge)
		declare -g KERNEL_MAJOR_MINOR="7.0"
		declare -g LINUXCONFIG="linux-${LINUXFAMILY}-edge"
		# CV1800B DMA support is not upstream until 7.1; backport it here.
		declare -g KERNELPATCHDIR="sophgo-sg200x-common sophgo-sg200x-dmac-7.0"
		;;

	bleedingedge)
		declare -g KERNEL_MAJOR_MINOR="7.1"
		declare -g LINUXCONFIG="linux-${LINUXFAMILY}-bleedingedge"
		declare -g KERNELPATCHDIR="sophgo-sg200x-common"
		;;

esac

# The AIC8800 Wi-Fi module is handled by the sophgo-aic8800 extension, and
# 512MB of RAM does not leave room to build the rest of Armbian's WiFi drivers.
declare -g EXTRAWIFI="no"

declare -g SERIALCON="ttyS0"

# The SG2000/SG2002 top out at 512MB, so zram matters a lot more than usual and
# the tuning is the same whichever core is running. The stock 50% gives ~240MB
# of swap; 100% gives a 512MB device while MEM_LIMIT_PERCENTAGE stays at its 50%
# default, so zram may still only occupy 256MB of physical RAM. That is the
# overcommit setup the shipped config file points at.
#
# The variable is ZRAM_PERCENTAGE and it ships commented out, so the pattern has
# to tolerate the leading "# " - an earlier version anchored on ^PERCENTAGE= and
# silently matched nothing for every build.
function post_family_tweaks__sophgo_sg200x_zram() {
	display_alert "Sophgo SG200x" "tuning zram for 512MB of RAM" "info"

	declare zram_conf="${SDCARD}/etc/default/armbian-zram-config"
	if [[ -f "${zram_conf}" ]]; then
		sed -i 's/^#\?\s*ZRAM_PERCENTAGE=.*/ZRAM_PERCENTAGE=100/' "${zram_conf}"
		grep -q '^ZRAM_PERCENTAGE=100$' "${zram_conf}" ||
			exit_with_error "failed to set ZRAM_PERCENTAGE" "${zram_conf}"
	fi
}
