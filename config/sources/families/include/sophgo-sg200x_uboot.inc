#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2013-2026 Igor Pecovnik, igor@armbian.com
#
# This file is a part of the Armbian Build Framework
# https://github.com/armbian/build/
#
# Building the Sophgo SG200x bootloader from source.
#
# The SG200x BootROM only ever loads a file literally named fip.bin from the
# first FAT partition of the card, and the DDR init lives in the FSBL inside
# that container - there is no other entry point. Mainline U-Boot has no ARM
# Sophgo support at all (board/sophgo/ is riscv-only), and even its RISC-V
# targets rely on a vendor FSBL to bring up the clocks and load U-Boot. So a
# fip.bin is unavoidable; what we do here is build the whole thing ourselves
# rather than ship a prebuilt binary.
#
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
# The FSBL also decides which core runs Linux, through TOC_HEADER_NAME
# (0xC906B001 for RISC-V, 0xAA640001 for aarch64). Nothing on the board selects
# it; writing a different image is the switch.
#
# Both architectures skip FreeRTOS for the companion C906L core - Linux loads it
# later through remoteproc anyway, and building it would pull in a 131MiB repo
# plus cmake.
#
# The vendor U-Boot does not build standalone: its board description lives in
# the SDK, not in the U-Boot tree. Those files are vendored under
# packages/sophgo-sg200x/u-boot/ and copied in below. See that directory's
# README.md.
#
# None of the three build with a modern toolchain untouched. binutils 2.38 split
# zicsr and zifencei out of the base ISA, and each component needs a different
# answer: a backported upstream fix for U-Boot and a pair of patches for the
# FSBL, both under patch/, and a PLATFORM_RISCV_ISA override for OpenSBI, in
# sophgo_sg200x_build_opensbi() below.
#

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

# "yes" selects the 1050MHz OD_CLK_SEL variant over the 850MHz vendor default.
# This replaces queenkjuul's fsbl patch 0002, which hardcoded the same thing.
declare -g SOPHGO_CPU_OVERDRIVE="${SOPHGO_CPU_OVERDRIVE:-no}"

# Where the vendored cvitek board files live.
declare -g SOPHGO_CVI_FILES_DIR="${SRC}/packages/sophgo-sg200x/u-boot"

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
	run_host_command_logged cp -v "${files}/${variant}/include/cvipart.h" "include/"
	run_host_command_logged cp -v "${files}/${variant}/include/imgs.h" "include/"

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
