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
# The two families differ only in which core of the SG2000 runs Linux, and
# nothing in the kernel or the bootloader sources decides that. The board does,
# through a slide switch, and the image has to agree with it - see the board
# files. So almost every knob here is identical on both sides, and keeping two
# copies of it only invites drift: the U-Boot and FSBL commit pins in particular
# have to move together or the two images stop being the same board.
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
# See u-boot.md in the milkv-duos-docs repo. The per-arch half of this set -
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
#   - CONFIG_BOOTCOMMAND. "sd" gets "run distro_bootcmd || run sdboot"; "emmc"
#     gets "cvi_update || run distro_bootcmd || ...", where cvi_update is the
#     vendor eMMC flasher. On an eMMC boot the flasher finds no SD magic and
#     returns non-zero, so the '||' falls through and the image boots the
#     ordinary distroboot way.
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
# and what comes out is an ordinary bootable Armbian image, just with the eMMC
# bootloader in it. Getting it onto the eMMC is two writes rather than one,
# because no disk image can describe the whole eMMC: the BootROM reads fip.bin
# raw out of hardware boot partition 1 (/dev/mmcblkXboot0), a separate LUN that
# is not part of the addressable device an .img covers. So dd alone leaves the
# system in place and the board dead. /usr/sbin/sophgo-emmc-install, shipped in
# every image of this family, does both halves from a running SD system.
#
# For a board with no bootable card to run that from, the same image can be
# turned into a self-flashing installer card instead:
#
#   ./compile.sh build BOARD=milkv-duos-arm BRANCH=edge SOPHGO_CVI_STORAGE=emmc \
#       ENABLE_EXTENSIONS=sophgo-sg200x-emmc-installer
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
	sd | emmc) ;;
	*) exit_with_error "SOPHGO_CVI_STORAGE must be 'sd' or 'emmc'" "${SOPHGO_CVI_STORAGE}" ;;
esac

# The board name no longer says which of the two an image is - that was the
# point of dropping the -emmc board files - so the file name has to, or an SD
# image and an eMMC one differ only in content and are trivially mixed up once
# they are both sitting in output/images.
#
# It has to be a hook and it has to be this hook: EXTRA_IMAGE_SUFFIXES is
# declared in main-config.sh well after this include is sourced, and consolidated
# a few lines after extension_prepare_config runs. The 100_ prefix is what keeps
# "-emmc" ahead of the installer extension's "-installer" - unprefixed hook
# functions sort as 500_, so without it the suffixes would come out reversed.
function extension_prepare_config__100_sophgo_sg200x_storage_suffix() {
	[[ "${SOPHGO_CVI_STORAGE}" == "emmc" ]] || return 0
	EXTRA_IMAGE_SUFFIXES+=("-emmc")
}

# Kernel: mainline plus the out-of-tree Sophgo patch series.
#
# One series for both architectures. arch/arm64/boot/dts/sophgo/sg2000.dtsi
# #includes cv180x.dtsi and cv181x.dtsi straight out of arch/riscv, redefining
# SOC_PERIPHERAL_IRQ() to emit GIC_SPI specifiers, so the entire SoC description
# is shared and so are the patches that touch it. The handful of arm64-only DTS
# patches in there are inert on a RISC-V build, which never descends into
# arch/arm64. See kernel-patches.md in the milkv-duos-docs repo.
case "${BRANCH}" in

	edge)
		declare -g KERNEL_MAJOR_MINOR="7.0"
		declare -g LINUXCONFIG="linux-${LINUXFAMILY}-edge"
		# CV1800B DMA support is not upstream until 7.1, and the SG2000 DT
		# bindings not until 7.2; both are carried here for 7.0 only.
		declare -g KERNELPATCHDIR="sophgo-sg200x-common sophgo-sg200x-dmac-7.0 sophgo-sg200x-bindings-7.0"
		;;

	# Bleeding edge follows the current release candidate, so it moves; that is
	# the point of the branch. The tag has to be pinned here rather than left to
	# the framework, because the framework only knows one rc at a time:
	# config/sources/mainline-kernel.conf.sh names whichever rc it was last
	# bumped to (7.1-rc7 in this tree), and for anything else its 900_defaults
	# hook falls back to branch:linux-7.2.y - a stable branch that does not exist
	# until 7.2 is released, so the fetch would simply fail. Setting KERNELBRANCH
	# here wins outright: common.conf only calls the mainline_kernel_decide_version
	# hooks when it is empty.
	#
	# Bump the tag by hand as rcs land. Once 7.2 is out, drop the pin entirely and
	# the stable branch takes over on its own.
	bleedingedge)
		declare -g KERNEL_MAJOR_MINOR="7.2"
		declare -g KERNELBRANCH="tag:v7.2-rc5"
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
