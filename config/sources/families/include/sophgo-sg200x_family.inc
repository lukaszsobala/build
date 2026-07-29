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

# That vendor U-Boot is patched for distroboot, so it finds and parses
# /extlinux/extlinux.conf on the first bootable partition by itself.
declare -g SRC_EXTLINUX="yes"

# Identifies the duo-buildroot-sdk-v2 project whose board files we vendored.
# See packages/sophgo-sg200x/u-boot/README.md. The per-arch half of this set -
# SOPHGO_CVI_PROJECT, _BOARD, _FILES_VARIANT, _UBOOT_DTS_DIR, _UBOOT_ARCH and
# SOPHGO_FSBL_ARCH - lives in the family files.
declare -g SOPHGO_CVI_CHIP="sg2000"
declare -g SOPHGO_CVI_CHIP_ARCH="CV181X"
declare -g SOPHGO_CVI_DDR_CFG="ddr3_1866_x16"
declare -g SOPHGO_CVI_STORAGE="sd"
declare -g SOPHGO_CVI_PANEL="MIPI_panel_milkv_8hd"

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
