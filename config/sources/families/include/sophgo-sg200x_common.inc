#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2013-2026 Igor Pecovnik, igor@armbian.com
#
# This file is a part of the Armbian Build Framework
# https://github.com/armbian/build/
#
# Everything the Sophgo SG200x families have in common.
#
# The SG2000/SG2002 (CV181x) pairs an ARM Cortex-A53 with a T-Head C906 RISC-V
# core, and only one of them runs Linux. Nothing in the kernel or the bootloader
# sources decides which: the board does, through a slide switch, and the image
# has to agree with it. Hence two families, sophgo-sg200x-arm64.conf and
# sophgo-sg200x-riscv64.conf - and almost everything in them is identical.
# Keeping two copies would only invite drift; the U-Boot and FSBL commit pins in
# particular have to move together or the two images stop being the same board.
#
# So all of it is here, and the family files carry just the identity of the core
# they run on. What genuinely differs is the vendor SDK's naming for the two
# builds, and that is a case on ARCH below rather than a second copy of the file.
#
# Sourced at the end of both family files, which set ARCH and LINUXFAMILY first;
# both are read here, at source time.
#
# Sections, in dependency order:
#
#   kernel               branches and patch series
#   bootloader sources   repos and commit pins for U-Boot, the FSBL and OpenSBI
#   vendor SDK identity  the SOPHGO_CVI_* set, including the per-ARCH case
#   U-Boot build         BOOTCONFIG and the hooks that produce fip.bin
#   image layout         the FAT /boot the BootROM needs, and fip.bin in it
#   userspace            BSP files and family tweaks
#

declare -g GOVERNOR="performance"
declare -g SERIALCON="ttyS0"

# ---------------------------------------------------------------------------
# Kernel
# ---------------------------------------------------------------------------

# Mainline plus the out-of-tree Sophgo patch series.
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
		;;

esac

# One patch directory per kernel version, the way every other mainline family
# does it. The series is otherwise identical between the two, so a new version
# starts as a copy of the previous one: patches that have landed upstream are
# deleted, the rest are refreshed. 7.0 additionally carries four backports that
# 7.2 gets from upstream - the CV1800B DMA driver, upstream in 7.1, and the
# SG2000 DT bindings, upstream in 7.2 - which is why the numbering in the 7.2
# directory has gaps where 0001, 0003, 0024 and 0025 would be. Keeping the
# numbers aligned across directories is deliberate: it makes the two series
# diffable by filename.
declare -g KERNELPATCHDIR="archive/sophgo-sg200x-${KERNEL_MAJOR_MINOR}" # Don't use symlinks.

# The AIC8800 Wi-Fi module is handled by the sophgo-sg200x-aic8800 extension, and
# 512MB of RAM does not leave room to build the rest of Armbian's WiFi drivers.
declare -g EXTRAWIFI="no"

# Overlays are installed by the kernel package to /boot/dtb/sophgo/overlay/ and
# named ${OVERLAY_PREFIX}-${name}.dtbo, which is the path boot.cmd builds. The
# prefix is the SoC rather than the family, because the .dtso sources live in
# arch/*/boot/dts/sophgo/overlay/ next to the board DTS they patch.
declare -g OVERLAY_PREFIX="sg2000"

# ---------------------------------------------------------------------------
# Bootloader sources
# ---------------------------------------------------------------------------

# Built from source on both sides.
#
# The SG200x BootROM only ever loads a file literally named fip.bin from the
# first FAT partition of the card, and the DDR init lives in the FSBL inside
# that container - there is no other entry point. Mainline U-Boot has no ARM
# Sophgo support at all (board/sophgo/ is riscv-only), and even its RISC-V
# targets rely on a vendor FSBL to bring up the clocks and load U-Boot. So a
# fip.bin is unavoidable; what we do is build the whole thing ourselves rather
# than ship a prebuilt binary. See the U-Boot build section below for the chain.
declare -g BOOTSOURCE="https://github.com/sophgo/u-boot-2021.10"
declare -g BOOTDIR="u-boot-sophgo-sg200x"
declare -g BOOTBRANCH="commit:bb7496c2dfded43921695bf49c23da28381f8960" # sg200x-dev, 2026-06-30
declare -g BOOTPATCHDIR="u-boot-sophgo-sg200x"

# The FSBL consumes u-boot.bin, so it cannot be built by compile_atf() - that
# runs before u-boot. It is built from uboot_custom_postprocess() instead, and
# so is OpenSBI, which has to see u-boot's device tree.
declare -g ATFSOURCE="none"

declare -g SOPHGO_FSBL_REPO="${SOPHGO_FSBL_REPO:-https://github.com/sophgo/fsbl}"
declare -g SOPHGO_FSBL_REF="${SOPHGO_FSBL_REF:-commit:3c47dfc743ad6cee06bca7161b352673e5c63356}" # sg200x-dev, 2026-06-30

# RISC-V only: the EL3 monitor slot in fip.bin holds OpenSBI instead of the
# prebuilt bl31.bin that ARM uses. This is sophgo/opensbi's sg200x-dev branch,
# which is OpenSBI 0.9 plus a cvitek platform override - byte-identical to the
# opensbi/ tree inside duo-buildroot-sdk-v2. Note that the same repo's master
# branch is plain upstream OpenSBI 1.7 with no Sophgo support at all, so the
# branch here matters more than usual.
declare -g SOPHGO_OPENSBI_REPO="${SOPHGO_OPENSBI_REPO:-https://github.com/sophgo/opensbi}"
declare -g SOPHGO_OPENSBI_REF="${SOPHGO_OPENSBI_REF:-commit:6766e9cc96df51cde91540f9a15f291bf843b682}" # sg200x-dev, 2025-01-24

# CPU overdrive: "yes" builds the FSBL with OD_CLK_SEL=y, which makes bl2_main
# pick CLK_OD and so sys_pll_od() instead of sys_pll_nd() - 1050MHz rather than
# the 850MHz vendor default. It also feeds plat/cv181x/ddr/ddr.mk, so the DDR
# timing is built to match; this is a different bootloader binary, not a knob
# Linux can turn. Replaces queenkjuul's fsbl patch 0002, which hardcoded it.
#
# Default is "yes". 1GHz is the speed the SG2000 is sold at and the speed both
# the Milk-V and the vendor SDK images run at, so the 850MHz setting is the
# surprising one, not this. Drop back to it with
#
#     ./compile.sh build BOARD=milkv-duos-riscv BRANCH=edge SOPHGO_CPU_OVERDRIVE=no
#
# It is listed in artifact-uboot.sh's vars_to_hash, so the two settings produce
# different u-boot artifact versions and both can sit in the cache at once.
# Without that it would be silently ignored on any build that had a cached
# artifact, which is every build after the first.
declare -g SOPHGO_CPU_OVERDRIVE="${SOPHGO_CPU_OVERDRIVE:-yes}"

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

# ---------------------------------------------------------------------------
# Vendor SDK identity
# ---------------------------------------------------------------------------

# Identifies the duo-buildroot-sdk-v2 project whose board files we vendored.
# See u-boot.md in the milkv-duos-docs repo.
declare -g SOPHGO_CVI_CHIP="sg2000"
declare -g SOPHGO_CVI_CHIP_ARCH="CV181X"
declare -g SOPHGO_CVI_DDR_CFG="ddr3_1866_x16"
declare -g SOPHGO_CVI_PANEL="MIPI_panel_milkv_8hd"

# Where the vendored cvitek board files live.
declare -g SOPHGO_CVI_FILES_DIR="${SRC}/packages/sophgo-sg200x/u-boot"

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

# The half of the vendored board files that is named after the core, and the
# only thing in this file that differs between the two architectures.
#
# The _sd/_emmc suffix comes from SOPHGO_CVI_STORAGE above, so a board file
# selects the whole set - defconfig, board DTS, imgs.h, cvipart.h - by setting
# that one variable. It also feeds BOOTCONFIG, which is the first entry in
# artifact-uboot.sh's vars_to_hash, so the two storage variants get distinct
# u-boot artifact versions without touching that list.
#
# ARCH is set by the family file before this include is sourced.
case "${ARCH}" in

	arm64)
		declare -g SOPHGO_CVI_PROJECT="sg2000_milkv_duos_glibc_arm64_${SOPHGO_CVI_STORAGE}"
		declare -g SOPHGO_CVI_BOARD="milkv_duos_glibc_arm64_${SOPHGO_CVI_STORAGE}"
		declare -g SOPHGO_CVI_FILES_VARIANT="arm64"
		# U-Boot 2021.10 has no arch/arm64; aarch64 boards live under arch/arm.
		declare -g SOPHGO_CVI_UBOOT_DTS_DIR="arch/arm/dts"
		declare -g SOPHGO_CVI_UBOOT_ARCH="arm64"
		# Also selects the prebuilt bl31.bin from inside the FSBL tree for
		# fip.bin's monitor slot, where RISC-V builds OpenSBI.
		declare -g SOPHGO_FSBL_ARCH="arm64"
		;;

	riscv64)
		# Note the musl project name: that is the SDK's own naming for the
		# RISC-V build and has no bearing on the Armbian userspace, which is
		# glibc either way.
		declare -g SOPHGO_CVI_PROJECT="sg2000_milkv_duos_musl_riscv64_${SOPHGO_CVI_STORAGE}"
		declare -g SOPHGO_CVI_BOARD="milkv_duos_musl_riscv64_${SOPHGO_CVI_STORAGE}"
		declare -g SOPHGO_CVI_FILES_VARIANT="riscv64"
		declare -g SOPHGO_CVI_UBOOT_DTS_DIR="arch/riscv/dts"
		# "riscv", not "riscv64": both the SDK's CONFIG_ARCH and the FSBL
		# Makefile's 'ifeq ($(ARCH),riscv)' expect the short name, and the FSBL
		# falls back to aarch64 for anything else.
		declare -g SOPHGO_CVI_UBOOT_ARCH="riscv"
		# Also selects OpenSBI for fip.bin's monitor slot, where ARM uses the
		# prebuilt bl31.bin shipped inside the FSBL tree.
		declare -g SOPHGO_FSBL_ARCH="riscv"
		;;

	*)
		exit_with_error "sophgo-sg200x: unsupported ARCH" "${ARCH}"
		;;

esac

# ---------------------------------------------------------------------------
# U-Boot build
# ---------------------------------------------------------------------------

# The chain, distilled from duo-buildroot-sdk-v2's build/scripts/fip_v2.mk:
#
#   u-boot  (sophgo/u-boot-2021.10)               ->  u-boot.bin
#     cat u-boot.bin > u-boot-raw.bin
#   opensbi (sophgo/opensbi, RISC-V only)         ->  fw_dynamic.bin
#   fsbl    (sophgo/fsbl, LOADER_2ND_PATH=above,
#            MONITOR_PATH=bl31.bin or fw_dynamic) ->  fip.bin
#
# fip.bin has a monitor slot and a "loader 2nd" slot. The loader is always
# U-Boot. The monitor is a 24KiB prebuilt bl31.bin shipped inside sophgo/fsbl
# (plat/cv181x/prebuilt/) on ARM, and OpenSBI on RISC-V, which is why only the
# RISC-V family builds a third component.
#
# The FSBL is built for one core and says which through TOC_HEADER_NAME
# (0xC906B001 for RISC-V, 0xAA640001 for aarch64); plat/cv181x/platform.c is
# full of '#ifdef __riscv' besides. So a given fip.bin serves one architecture
# and not the other.
#
# It does not get to choose, though. The slide switch on the board decides which
# core the BootROM comes up on, and the BootROM then wants a fip.bin whose TOC
# header matches. Both have to agree: the switch position and which of the two
# board images was written. A mismatch does not boot at all.
#
# Both architectures skip FreeRTOS for the companion C906L core - Linux loads it
# later through remoteproc anyway, and building it would pull in a 131MiB repo
# plus cmake.
#
# The vendor U-Boot does not build standalone: its board description lives in
# the SDK, not in the U-Boot tree. Those files are vendored under
# packages/sophgo-sg200x/u-boot/ and copied in below. See u-boot.md in the
# milkv-duos-docs repo.
#
# None of the three build with a modern toolchain untouched. binutils 2.38 split
# zicsr and zifencei out of the base ISA, and each component needs a different
# answer: a backported upstream fix for U-Boot and a pair of patches for the
# FSBL, both under patch/, and a PLATFORM_RISCV_ISA override for OpenSBI, in
# sophgo_sg200x_build_opensbi() below.

# Only u-boot.bin is a real make target; fip.bin is produced by
# uboot_custom_postprocess() below and picked up from the same directory.
declare -g UBOOT_TARGET_MAP=";;u-boot.bin fip.bin"

# The vendor defconfig is named after the SDK project.
declare -g BOOTCONFIG="cvitek_${SOPHGO_CVI_PROJECT}_defconfig"

function fetch_custom_uboot__sophgo_sg200x_fsbl() {
	display_alert "Sophgo SG200x" "fetching FSBL sources" "info"
	fetch_from_repo "${SOPHGO_FSBL_REPO}" "sophgo-fsbl" "${SOPHGO_FSBL_REF}" "yes"
	declare fsbl_subdir
	fsbl_subdir="$(branch2dir "${SOPHGO_FSBL_REF}")"
	declare -g SOPHGO_FSBL_DIR="${SRC}/cache/sources/sophgo-fsbl/${fsbl_subdir}"

	sophgo_sg200x_apply_patches "fsbl-sophgo-sg200x" "${SOPHGO_FSBL_DIR}" "FSBL"

	[[ "${SOPHGO_FSBL_ARCH}" == "riscv" ]] || return 0

	display_alert "Sophgo SG200x" "fetching OpenSBI sources" "info"
	fetch_from_repo "${SOPHGO_OPENSBI_REPO}" "sophgo-opensbi" "${SOPHGO_OPENSBI_REF}" "yes"
	declare opensbi_subdir
	opensbi_subdir="$(branch2dir "${SOPHGO_OPENSBI_REF}")"
	declare -g SOPHGO_OPENSBI_DIR="${SRC}/cache/sources/sophgo-opensbi/${opensbi_subdir}"

	sophgo_sg200x_apply_patches "opensbi-sophgo-sg200x" "${SOPHGO_OPENSBI_DIR}" "OpenSBI"
}

# Neither the FSBL nor OpenSBI has Armbian patch machinery behind it: ATFSOURCE
# is "none", so compile_atf() - and with it ATFPATCHDIR and advanced_patch() -
# never runs. Both sets live under patch/atf/ regardless, because both fill the
# monitor role that bl31.bin fills on ARM, and that is where a reader will look.
#
# fetch_from_repo() has just done a 'git checkout -f' plus 'git clean -df', so
# each tree is pristine on every build and a plain apply is enough; the
# already-applied branch only exists so a re-run inside one build does not
# produce a baffling failure.
function sophgo_sg200x_apply_patches() {
	declare patch_dir="${SRC}/patch/atf/${1}"
	declare target_dir="${2}"
	declare label="${3}"
	[[ -d "${patch_dir}" ]] || return 0

	declare patch
	for patch in "${patch_dir}"/*.patch; do
		[[ -f "${patch}" ]] || continue
		if git -C "${target_dir}" apply --check "${patch}" &> /dev/null; then
			display_alert "Sophgo SG200x" "${label} patch: $(basename "${patch}")" "info"
			run_host_command_logged git -C "${target_dir}" apply "${patch}"
		elif git -C "${target_dir}" apply --reverse --check "${patch}" &> /dev/null; then
			display_alert "Sophgo SG200x" "${label} patch already applied: $(basename "${patch}")" "info"
		else
			exit_with_error "${label} patch does not apply" "$(basename "${patch}")"
		fi
	done
}

# Runs in the u-boot tree, after patching (which resets and cleans the tree) and
# before 'make <defconfig>' - so the copies survive and the defconfig is in
# place in time.
function pre_config_uboot_target__sophgo_sg200x_board_files() {
	declare files="${SOPHGO_CVI_FILES_DIR}"
	declare variant="${SOPHGO_CVI_FILES_VARIANT}"

	display_alert "Sophgo SG200x" "installing cvitek board files (${variant})" "info"

	run_host_command_logged cp -v "${files}/common/dts/"*.dtsi "${SOPHGO_CVI_UBOOT_DTS_DIR}/"
	run_host_command_logged cp -v "${files}/${variant}/dts/"* "${SOPHGO_CVI_UBOOT_DTS_DIR}/"
	run_host_command_logged cp -v "${files}/${variant}/configs/"* "configs/"
	run_host_command_logged cp -v "${files}/${variant}/board/cvi_board_init.c" "board/cvitek/"
	run_host_command_logged mkdir -p "include/cvitek"
	run_host_command_logged cp -v "${files}/${variant}/include/cvitek.h" "include/cvitek/"
	run_host_command_logged cp -v "${files}/${variant}/include/cvi_board_memmap.h" "include/"

	# Both dts/ and configs/ are copied wholesale above - the SD and eMMC board
	# files coexist there harmlessly, and CVIBOARD/BOOTCONFIG pick the pair that
	# gets built. These two cannot work that way: the names are fixed, U-Boot
	# #includes them unconditionally, and their contents are per-storage. The
	# SDK generates them per project for the same reason.
	declare storage="${SOPHGO_CVI_STORAGE}"
	run_host_command_logged cp -v "${files}/${variant}/include/${storage}/cvipart.h" "include/"
	run_host_command_logged cp -v "${files}/${variant}/include/${storage}/imgs.h" "include/"

	# cvitek.mk in the u-boot tree turns these into -D flags, and dts/Makefile
	# builds arch/$(ARCH)/dts/$(CHIP)_$(CVIBOARD).dtb from them. Exporting here
	# covers 'make <defconfig>' and 'make olddefconfig', which Armbian runs with
	# the ambient environment; the actual compile runs under 'env -i' and is
	# handled by the uboot_make_config hook below.
	sophgo_sg200x_export_cvitek_env
}

# The single source of truth for the vendor build environment. Captured from
# duo-buildroot-sdk-v2's build/Makefile and build/envsetup_soc.sh for the
# sg2000_milkv_duos_glibc_arm64_sd project.
#
# Fills the caller's `sophgo_cvitek_env` array by dynamic scoping - so no
# `declare -g` here, which would write to the global and leave the caller's
# local empty.
function sophgo_sg200x_cvitek_env_array() {
	sophgo_cvitek_env=(
		"ARCH=${SOPHGO_CVI_UBOOT_ARCH}"
		"CHIP=${SOPHGO_CVI_CHIP}"
		"CHIP_ARCH=${SOPHGO_CVI_CHIP_ARCH}"
		"CVIBOARD=${SOPHGO_CVI_BOARD}"
		"STORAGE_TYPE=${SOPHGO_CVI_STORAGE}"
		"PANEL_TUNING_PARAM=${SOPHGO_CVI_PANEL}"
		"CONFIG_SKIP_RAMDISK=y"
		"CONFIG_USE_DEFAULT_ENV=y"
		"RELEASE=0"
		"UBOOT_VBOOT=0"
	)
}

function sophgo_sg200x_export_cvitek_env() {
	# ARCH is the awkward one: Armbian's is "riscv64"/"arm64", the vendor build
	# wants "riscv"/"arm64". Overwriting it in the ambient environment is what
	# makes 'make <defconfig>' work, but it must not still be overwritten when
	# the .deb is built - dpkg then stamps "Architecture: riscv" and refuses to
	# install it with "package architecture (riscv) does not match system
	# (riscv64)". uboot_custom_postprocess() puts it back; see there.
	declare -g SOPHGO_SAVED_ARCH="${ARCH}"

	declare -a sophgo_cvitek_env
	sophgo_sg200x_cvitek_env_array
	declare kv
	for kv in "${sophgo_cvitek_env[@]}"; do
		export "${kv?}"
	done
}

function uboot_make_config__sophgo_sg200x_cvitek_env() {
	declare -a sophgo_cvitek_env
	sophgo_sg200x_cvitek_env_array
	# outer scope array, consumed by 'env -i "${uboot_make_envs[@]}"'
	uboot_make_envs+=("${sophgo_cvitek_env[@]}")
}

function post_config_uboot_target__sophgo_sg200x_cflags() {
	# U-Boot 2021.10 predates gcc 13's -Wenum-int-mismatch; common/command.c
	# declares cmd_process() returning int against an enum prototype.
	# outer scope array, from compile_uboot_target()
	uboot_cflags_array+=("-Wno-error=enum-int-mismatch")
}

# Runs in the u-boot tree right after a successful build. Produces fip.bin,
# which UBOOT_TARGET_MAP's target_files then packages into the .deb.
function uboot_custom_postprocess() {
	: "${SOPHGO_FSBL_DIR:?SOPHGO_FSBL_DIR is not set - did fetch_custom_uboot run?}"

	# Hand ARCH back to Armbian. The vendor value set in
	# pre_config_uboot_target has done its job - 'make <defconfig>', 'make
	# olddefconfig' and the compile are all behind us, and the compile ran under
	# 'env -i' with its own copy anyway - while everything from here on is
	# Armbian's, starting with the .deb's Architecture field.
	if [[ -n "${SOPHGO_SAVED_ARCH}" && "${ARCH}" != "${SOPHGO_SAVED_ARCH}" ]]; then
		display_alert "Sophgo SG200x" "restoring ARCH=${SOPHGO_SAVED_ARCH} (was ${ARCH})" "info"
		export ARCH="${SOPHGO_SAVED_ARCH}"
	fi

	declare uboot_dir
	uboot_dir="$(pwd)"

	# fip_v2.mk feeds the FSBL an uncompressed copy under this name.
	run_host_command_logged cp -v "${uboot_dir}/u-boot.bin" "${uboot_dir}/u-boot-raw.bin"

	# plat/*/include/mmap.h does #include "cvi_board_memmap.h", and the FSBL
	# Makefile has -Ibuild, so the memory map has to land there.
	run_host_command_logged mkdir -p "${SOPHGO_FSBL_DIR}/build"
	run_host_command_logged cp -v \
		"${SOPHGO_CVI_FILES_DIR}/${SOPHGO_CVI_FILES_VARIANT}/include/cvi_board_memmap.h" \
		"${SOPHGO_FSBL_DIR}/build/"

	declare fsbl_out="${SOPHGO_FSBL_DIR}/build/${SOPHGO_CVI_PROJECT}"
	declare od_clk_sel="n"
	[[ "${SOPHGO_CPU_OVERDRIVE}" == "yes" ]] && od_clk_sel="y"

	# The monitor slot in fip.bin: a prebuilt bl31.bin on ARM (the FSBL picks it
	# up from plat/*/prebuilt by itself), OpenSBI built here on RISC-V.
	declare -a fsbl_monitor=()
	if [[ "${SOPHGO_FSBL_ARCH}" == "riscv" ]]; then
		sophgo_sg200x_build_opensbi "${uboot_dir}"
		fsbl_monitor=("MONITOR_PATH=${SOPHGO_OPENSBI_FW_DYNAMIC}")
	fi

	display_alert "Sophgo SG200x" "building FSBL and packing fip.bin (OD_CLK_SEL=${od_clk_sel})" "info"

	# Which variables go through the environment and which on the command line
	# is not cosmetic. The FSBL Makefile does
	#     CHIP_ARCH := $(shell echo '${CHIP_ARCH}' | tr A-Z a-z)
	# to derive the platform directory name, and a make command-line variable is
	# immutable - passing CHIP_ARCH there defeats that assignment and the build
	# dies with "Invalid platform". fip_v2.mk exports it instead, so we do too,
	# and keep the command line to exactly what the SDK puts there.
	#
	# LDFLAGS: the FSBL links with --fatal-warnings, and binutils >= 2.39 warns
	# about the RWX LOAD segment its linker script produces. Nothing in fsbl
	# assigns LDFLAGS, so the command line is safe for it.
	#
	# CROSS_COMPILE is deliberately not passed. The FSBL Makefile has
	#     ifneq ($(origin CROSS_COMPILE),command line)
	#       CROSS_COMPILE := ${CROSS_COMPILE_64}   # or _GLIBC_RISCV64
	#       BOOT_CPU := aarch64 ; ARCH := aarch64
	#     endif
	# so setting CROSS_COMPILE in the environment gets it overwritten by an
	# empty CROSS_COMPILE_64, while setting it on the command line would skip
	# the BOOT_CPU/ARCH resolution with it. Feed the per-arch variable instead
	# and let the Makefile derive the rest, exactly as envsetup_soc.sh does.
	run_host_command_logged env \
		"ARCH=${SOPHGO_FSBL_ARCH}" \
		"CROSS_COMPILE_64=${UBOOT_COMPILER}" \
		"CROSS_COMPILE_GLIBC_RISCV64=${UBOOT_COMPILER}" \
		"CHIP_ARCH=${SOPHGO_CVI_CHIP_ARCH}" \
		"CHIP=${SOPHGO_CVI_CHIP}" \
		"DDR_CFG=${SOPHGO_CVI_DDR_CFG}" \
		"STORAGE=${SOPHGO_CVI_STORAGE}" \
		"STORAGE_TYPE=${SOPHGO_CVI_STORAGE}" \
		"OD_CLK_SEL=${od_clk_sel}" \
		"PROJECT_FULLNAME=${SOPHGO_CVI_PROJECT}" \
		make -C "${SOPHGO_FSBL_DIR}" \
		"O=${fsbl_out}" \
		"LOG_LEVEL=2" \
		"BLCP_2ND_PATH=" \
		"LOADER_2ND_PATH=${uboot_dir}/u-boot-raw.bin" \
		"LDFLAGS=--no-warn-rwx-segments" \
		"${fsbl_monitor[@]}" \
		"${CTHREADS}"

	[[ -f "${fsbl_out}/fip.bin" ]] || exit_with_error "FSBL did not produce fip.bin" "${fsbl_out}"

	run_host_command_logged cp -v "${fsbl_out}/fip.bin" "${uboot_dir}/fip.bin"
	display_alert "Sophgo SG200x" "fip.bin is $(stat -c %s "${uboot_dir}/fip.bin") bytes" "info"
}

# RISC-V only. Builds the OpenSBI that goes in fip.bin's monitor slot, and sets
# SOPHGO_OPENSBI_FW_DYNAMIC to the result. Has to run after u-boot, because it
# embeds u-boot's own device tree - see FW_FDT_PATH below.
function sophgo_sg200x_build_opensbi() {
	declare uboot_dir="${1}"
	: "${SOPHGO_OPENSBI_DIR:?SOPHGO_OPENSBI_DIR is not set - did fetch_custom_uboot run?}"

	declare fdt="${uboot_dir}/arch/riscv/dts/${SOPHGO_CVI_CHIP}_${SOPHGO_CVI_BOARD}.dtb"
	[[ -f "${fdt}" ]] || exit_with_error "u-boot did not produce a device tree for OpenSBI" "${fdt}"

	display_alert "Sophgo SG200x" "building OpenSBI (monitor for fip.bin)" "info"

	# FW_FDT_PATH is not optional. fw_base.S does
	#     #ifdef FW_FDT_PATH
	#         lla a1, fw_fdt_bin      /* Override previous arg1 */
	#     #endif
	# so without it OpenSBI keeps whatever a1 the FSBL happened to leave behind,
	# which is not a device tree. The SDK points it at u-boot's dtb and so do we.
	#
	# PLATFORM_RISCV_ISA: OpenSBI 0.9 predates the binutils 2.38 split of zicsr
	# and zifencei out of 'i', and lib/sbi/sbi_tlb.c has a bare fence.i. The
	# default rv64imafdc therefore fails to assemble; spell the two out.
	#
	# platform-cflags-y carries -Wno-error=maybe-uninitialized for a gcc 13 false
	# positive in fdt_reset_thead.c (clone_csrs' cnt). It is assigned, empty, in
	# platform/generic/config.mk and nothing appends to it, so overriding it from
	# the command line drops nothing. firmware-cflags-y would have been the more
	# obvious hook, but it carries -fPIE -pie that we must not clobber.
	run_host_command_logged make -C "${SOPHGO_OPENSBI_DIR}" \
		"PLATFORM=generic" \
		"CROSS_COMPILE=${UBOOT_COMPILER}" \
		"CHIP_ARCH=${SOPHGO_CVI_CHIP_ARCH}" \
		"OPENSBI_PATH=${SOPHGO_OPENSBI_DIR}" \
		"PLATFORM_RISCV_ISA=rv64imafdc_zicsr_zifencei" \
		"platform-cflags-y=-Wno-error=maybe-uninitialized" \
		"FW_PAYLOAD_PATH=${uboot_dir}/u-boot-raw.bin" \
		"FW_FDT_PATH=${fdt}" \
		"${CTHREADS}"

	declare -g SOPHGO_OPENSBI_FW_DYNAMIC="${SOPHGO_OPENSBI_DIR}/build/platform/generic/firmware/fw_dynamic.bin"
	[[ -f "${SOPHGO_OPENSBI_FW_DYNAMIC}" ]] ||
		exit_with_error "OpenSBI did not produce fw_dynamic.bin" "${SOPHGO_OPENSBI_FW_DYNAMIC}"

	display_alert "Sophgo SG200x" \
		"OpenSBI fw_dynamic.bin is $(stat -c %s "${SOPHGO_OPENSBI_FW_DYNAMIC}") bytes" "info"
}

# ---------------------------------------------------------------------------
# Image layout
# ---------------------------------------------------------------------------

# Card layout produced here:
#
#   MBR
#   p1  FAT16, type 0x0c, bootable   /boot   fip.bin + kernel + initrd + dtb
#   p2  ext4                         /
#
# The BootROM looks for a file named fip.bin in the first FAT partition, so the
# partition table has to be MBR and p1 has to be a plain FAT type - not the
# 0xea "Linux extended boot" type Armbian would use by default.
#
# The vendor U-Boot inside fip.bin then runs distro_bootcmd, whose
# scan_dev_for_boot_part iterates the partitions marked bootable in the MBR
# (falling back to partition 1 when none are), and picks up /boot.scr from the
# FAT partition - scan_dev_for_boot looks for /extlinux/extlinux.conf first and
# we no longer write one, so it falls through to the script.

# /boot is a FAT partition because the BootROM has to be able to read fip.bin
# out of it. 128MiB matches the vendor image and leaves room for a few kernels.
declare -g BOOTFS_TYPE="fat"
declare -g BOOTSIZE="128"

function pre_prepare_partitions__sophgo_sg200x_msdos_layout() {
	# The BootROM parses an MBR. Only the RISC-V side actually needs this -
	# config/sources/riscv64.conf declares GPT, where arm64 falls through to the
	# "msdos" default in main-config.sh - but setting it for both keeps the two
	# images the same shape and survives that default changing.
	declare -g IMAGE_PARTITION_TABLE="msdos"
	# 1MiB, matching the vendor and upstream Milk-V images.
	declare -g OFFSET="1"
}

function post_create_partitions__sophgo_sg200x_boot_partition_flags() {
	display_alert "Sophgo SG200x" "marking boot partition FAT (0x0c) and bootable" "info"
	run_host_command_logged sfdisk --part-type "${SDCARD}.raw" 1 c
	run_host_command_logged sfdisk --activate "${SDCARD}.raw" 1
}

# Nothing to dd onto raw sectors: the BootROM reads fip.bin as a file from the
# FAT partition, not from a fixed offset. write_uboot_platform() only gets a
# block device, so the copy happens in the hook below instead.
function write_uboot_platform() {
	return 0
}

# Only fires when a bootloader was actually built (BOOTCONFIG != none). Runs
# with ${MOUNT} still mounted, and is handed the extracted u-boot package as
# UBOOT_CHROOT_DIR.
function post_write_uboot_platform__sophgo_sg200x_install_fip() {
	declare fip="${UBOOT_CHROOT_DIR}/fip.bin"

	if [[ ! -f "${fip}" ]]; then
		display_alert "Sophgo SG200x" "no fip.bin in the u-boot package - image will NOT boot" "wrn"
		return 0
	fi

	display_alert "Sophgo SG200x" "installing built fip.bin -> /boot/fip.bin" "info"
	run_host_command_logged cp -v "${fip}" "${MOUNT}/boot/fip.bin"

	# Stamp which of the two bootloaders this is, next to the file it describes.
	# Nothing at boot reads it; sophgo-emmc-install does, to refuse putting an SD
	# fip.bin into the eMMC boot partition - that one has cv-emmc@4300000 deleted
	# from its device tree, so it comes up with no eMMC and nothing to boot. The
	# two cannot be told apart from the file: U-Boot is LZMA-compressed inside
	# fip.bin, so not even the CONFIG_BOOTCOMMAND string survives to grep for.
	display_alert "Sophgo SG200x" "stamping /boot/sophgo-storage.txt as '${SOPHGO_CVI_STORAGE}'" "info"
	echo "${SOPHGO_CVI_STORAGE}" > "${MOUNT}/boot/sophgo-storage.txt"
}

# ---------------------------------------------------------------------------
# Userspace
# ---------------------------------------------------------------------------

# The USB CDC-NCM gadget has to hand an address to the computer at the other end
# of the cable, or that computer times out into a 169.254.x address and the board
# is unreachable to anyone who has not been told to configure it by hand.
# systemd-networkd needs nothing for that - DHCPServer= is built in - but
# NetworkManager's ipv4.method=shared runs a dnsmasq to do the same job, and
# Armbian installs with --no-install-recommends throughout, so the Recommends
# that would normally bring it along does not fire.
#
# Read here rather than in a hook because this is a package list: the framework
# computes NETWORKING_STACK before sourcing the family and freezes both this and
# PACKAGE_LIST_FAMILY afterwards, so a family file is the one place that can see
# one and still change the other.
if [[ "${NETWORKING_STACK}" == "network-manager" ]]; then
	declare -g PACKAGE_LIST_FAMILY="${PACKAGE_LIST_FAMILY} dnsmasq-base"
fi

# Shipped on both storage variants, because it is only ever useful from the one
# that is running: you boot a card and install to the eMMC from there. Harmless
# on an eMMC-booted system, where it refuses to touch its own root device.
function post_family_tweaks_bsp__sophgo_sg200x_emmc_install_tool() {
	display_alert "Sophgo SG200x" "installing eMMC install helper" "info"

	run_host_command_logged install -d -m 0755 "${destination}/usr/sbin"
	run_host_command_logged install -m 0755 \
		"${SRC}/packages/bsp/sophgo-sg200x/usr/sbin/sophgo-emmc-install" \
		"${destination}/usr/sbin/sophgo-emmc-install"
}

# Nothing gives the SG200x ethernet a MAC address - the device tree has none, the
# MAC's own filter is empty, and the vendor U-Boot defconfig sets
# CONFIG_NET_RANDOM_ETHADDR - so the kernel makes one up on every boot and the
# board's DHCP lease moves with it. Derive one from the chip's efuse serial
# instead. The helper is shared with the wifi side, which has the same problem for
# a different reason; extensions/sophgo-sg200x-aic8800.sh installs the rule for that and
# relies on the script landing here (both write into the same armbian-bsp-cli).
function post_family_tweaks_bsp__sophgo_sg200x_stable_mac() {
	display_alert "Sophgo SG200x" "installing stable MAC address helper" "info"

	run_host_command_logged install -d -m 0755 "${destination}/lib/udev" \
		"${destination}/etc/udev/rules.d"
	run_host_command_logged install -m 0755 \
		"${SRC}/packages/bsp/sophgo-sg200x/lib/udev/sg200x-stable-mac" \
		"${destination}/lib/udev/sg200x-stable-mac"
	run_host_command_logged install -m 0644 \
		"${SRC}/packages/bsp/sophgo-sg200x/etc/udev/rules.d/70-sg200x-stable-mac.rules" \
		"${destination}/etc/udev/rules.d/70-sg200x-stable-mac.rules"
}

# The userspace half of the "usb-device" device tree overlay: the overlay turns
# the USB controller around, and this describes what kind of device it should
# then pretend to be. See the script for why the two are independent and why this
# is installed on every image regardless of which overlays are enabled - in
# short, it does nothing at all until a UDC exists, and one only exists with the
# overlay on.
#
# Both network stacks' configuration goes in, because which one is running is not
# known here: NETWORKING_STACK defaults to systemd-networkd for BUILD_MINIMAL and
# NetworkManager otherwise, and either is a legitimate way to build this board.
# Neither stack reads the other's files.
function post_family_tweaks_bsp__sophgo_sg200x_usb_gadget() {
	display_alert "Sophgo SG200x" "installing USB CDC-NCM gadget service" "info"

	run_host_command_logged install -d -m 0755 "${destination}/usr/sbin" \
		"${destination}/usr/lib/systemd/system" \
		"${destination}/etc/systemd/network"
	run_host_command_logged install -m 0755 \
		"${SRC}/packages/bsp/sophgo-sg200x/usr/sbin/sg200x-usb-gadget" \
		"${destination}/usr/sbin/sg200x-usb-gadget"
	run_host_command_logged install -m 0644 \
		"${SRC}/packages/bsp/sophgo-sg200x/usr/lib/systemd/system/sg200x-usb-gadget.service" \
		"${destination}/usr/lib/systemd/system/sg200x-usb-gadget.service"
	run_host_command_logged install -m 0644 \
		"${SRC}/packages/bsp/sophgo-sg200x/etc/systemd/network/70-sg200x-usb0.network" \
		"${destination}/etc/systemd/network/70-sg200x-usb0.network"

	# 0600, and NetworkManager will not so much as read it otherwise.
	run_host_command_logged install -d -m 0755 "${destination}/etc/NetworkManager/system-connections"
	run_host_command_logged install -m 0600 \
		"${SRC}/packages/bsp/sophgo-sg200x/etc/NetworkManager/system-connections/sg200x-usb0.nmconnection" \
		"${destination}/etc/NetworkManager/system-connections/sg200x-usb0.nmconnection"
}

function post_family_tweaks__sophgo_sg200x_usb_gadget_enable() {
	display_alert "Sophgo SG200x" "enabling USB CDC-NCM gadget service" "info"
	chroot_sdcard systemctl --no-reload enable sg200x-usb-gadget.service
}

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
