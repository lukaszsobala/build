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
#   u-boot (sophgo/u-boot-2021.10)               ->  u-boot.bin
#     cat u-boot.bin > u-boot-raw.bin
#   fsbl   (sophgo/fsbl, LOADER_2ND_PATH=above)  ->  fip.bin
#
# On ARM that is the whole story: OpenSBI is only built for RISC-V, and the EL3
# monitor is a 24KiB prebuilt bl31.bin shipped inside sophgo/fsbl itself
# (plat/cv181x/prebuilt/). FreeRTOS for the companion C906L core is skipped -
# Linux loads it later through remoteproc anyway, and building it would pull in
# a 131MiB repo plus cmake.
#
# The vendor U-Boot does not build standalone: its board description lives in
# the SDK, not in the U-Boot tree. Those files are vendored under
# packages/sophgo-sg200x/u-boot/ and copied in below. See that directory's
# README.md.
#

declare -g SOPHGO_FSBL_REPO="${SOPHGO_FSBL_REPO:-https://github.com/sophgo/fsbl}"
declare -g SOPHGO_FSBL_REF="${SOPHGO_FSBL_REF:-commit:3c47dfc743ad6cee06bca7161b352673e5c63356}" # sg200x-dev, 2026-06-30

# Also declared by the sophgo-fip-blobs extension; default it here so the FSBL
# build does not depend on that extension being enabled. "yes" selects the
# 1050MHz OD_CLK_SEL variant over the 850MHz vendor default.
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
		"${CTHREADS}"

	[[ -f "${fsbl_out}/fip.bin" ]] || exit_with_error "FSBL did not produce fip.bin" "${fsbl_out}"

	run_host_command_logged cp -v "${fsbl_out}/fip.bin" "${uboot_dir}/fip.bin"
	display_alert "Sophgo SG200x" "fip.bin is $(stat -c %s "${uboot_dir}/fip.bin") bytes" "info"
}
