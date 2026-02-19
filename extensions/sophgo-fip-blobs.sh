#!/usr/bin/env bash
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2013-2026 Igor Pecovnik, igor@armbian.com
#
# This file is a part of the Armbian Build Framework
# https://github.com/armbian/build/
#
# Extension: sophgo-fip-blobs
#
# Handles installation of Sophgo SG2000/SG2002/CV1812H bootloader blobs (fip.bin)
# to the SD card image for boards like Milkv Duo S.
#
# The fip.bin contains FSBL + OpenSBI + U-Boot combined into a single image
# that must be written to raw sectors at the beginning of the SD card.
#
# This extension is automatically enabled for sophgo-sg2000 family boards via ENABLE_EXTENSIONS.
#

function extension_prepare_config__sophgo_fip_blobs() {
	# Only enable for Sophgo family boards
	[[ "${LINUXFAMILY}" != sophgo-sg2000* ]] && return 0

	display_alert "Extension: sophgo-fip-blobs" "Preparing Sophgo bootloader blob support" "info"

	# Define blob location based on board
	declare -g SOPHGO_FIP_BLOB_DIR="${SRC}/packages/blobs/sophgo"
}

# Find the appropriate fip.bin blob for the board
function sophgo_find_fip_blob() {
	local board_blob_dir=""

	# Determine board blob directory (strip architecture suffix for common blob)
	if [[ "${BOARD}" == *-arm ]]; then
		board_blob_dir="${SOPHGO_FIP_BLOB_DIR}/${BOARD%-arm}"
	elif [[ "${BOARD}" == *-riscv ]]; then
		board_blob_dir="${SOPHGO_FIP_BLOB_DIR}/${BOARD%-riscv}"
	else
		board_blob_dir="${SOPHGO_FIP_BLOB_DIR}/${BOARD}"
	fi

	# Check for architecture-specific blob first, then generic
	if [[ "${ARCH}" == "arm64" ]]; then
		[[ -f "${board_blob_dir}/fip-arm64.bin" ]] && echo "${board_blob_dir}/fip-arm64.bin" && return 0
	elif [[ "${ARCH}" == "riscv64" ]]; then
		[[ -f "${board_blob_dir}/fip-riscv.bin" ]] && echo "${board_blob_dir}/fip-riscv.bin" && return 0
	fi

	# Fallback to generic fip.bin
	[[ -f "${board_blob_dir}/fip.bin" ]] && echo "${board_blob_dir}/fip.bin" && return 0

	# Not found
	return 1
}

# Hook to copy fip.bin to the boot partition before unmounting
# The Sophgo bootrom reads fip.bin from the FAT boot partition as a file
function pre_umount_final_image__write_sophgo_fip_blob() {
	[[ "${LINUXFAMILY}" != sophgo-sg2000* ]] && return 0

	# Only run if BOOTCONFIG is "none" (vendor bootloader mode)
	[[ "${BOOTCONFIG}" != "none" ]] && return 0

	local fip_blob
	fip_blob=$(sophgo_find_fip_blob) || true

	if [[ -z "${fip_blob}" || ! -f "${fip_blob}" ]]; then
		display_alert "WARNING" "Sophgo fip.bin blob not found for ${BOARD}" "wrn"

		# Determine expected path for helpful message
		local expected_dir=""
		if [[ "${BOARD}" == *-arm ]]; then
			expected_dir="${SOPHGO_FIP_BLOB_DIR}/${BOARD%-arm}"
		elif [[ "${BOARD}" == *-riscv ]]; then
			expected_dir="${SOPHGO_FIP_BLOB_DIR}/${BOARD%-riscv}"
		else
			expected_dir="${SOPHGO_FIP_BLOB_DIR}/${BOARD}"
		fi

		display_alert "Expected location" "${expected_dir}/fip.bin" "wrn"
		display_alert "The image will be created WITHOUT bootloader" "" "wrn"
		display_alert "Manual fip.bin installation required before use" "" "wrn"
		display_alert "See README.md" "${expected_dir}/README.md" "info"
		return 0
	fi

	# Copy fip.bin to the boot partition as a file
	# The Sophgo bootrom reads fip.bin from the FAT filesystem
	local boot_mount="${MOUNT}/boot"

	display_alert "Copying Sophgo fip.bin" "${fip_blob} -> ${boot_mount}/fip.bin" "info"

	run_host_command_logged cp -v "${fip_blob}" "${boot_mount}/fip.bin"

	display_alert "Sophgo bootloader" "Copied to boot partition successfully" "info"
}

# Hook to create boot.sd script for Sophgo boards
# This is needed because SRC_EXTLINUX=yes prevents BOOTSCRIPT from being processed
# The boot.sd script sets up memory addresses and calls sysboot to parse extlinux.conf
function pre_umount_final_image__create_sophgo_boot_script() {
	[[ "${LINUXFAMILY}" != sophgo-sg2000* ]] && return 0

	local boot_mount="${MOUNT}/boot"
	local boot_script_src="${SRC}/config/bootscripts/boot-sophgo-sg2000.cmd"

	if [[ ! -f "${boot_script_src}" ]]; then
		display_alert "WARNING" "Sophgo boot script not found: ${boot_script_src}" "wrn"
		return 0
	fi

	display_alert "Creating boot.sd" "Compiling boot script for Sophgo" "info"

	# Copy boot.cmd to boot partition
	run_host_command_logged cp -v "${boot_script_src}" "${boot_mount}/boot.cmd"

	# Compile boot.cmd to boot.sd using mkimage
	# The vendor U-Boot looks for boot.sd specifically
	if [[ "${ARCH}" == "riscv64" ]]; then
		run_host_command_logged mkimage -A riscv -T script -C none -n "Boot script" \
			-d "${boot_mount}/boot.cmd" "${boot_mount}/boot.sd"
	else
		run_host_command_logged mkimage -A arm64 -T script -C none -n "Boot script" \
			-d "${boot_mount}/boot.cmd" "${boot_mount}/boot.sd"
	fi

	display_alert "Sophgo boot script" "boot.sd created successfully" "info"
}
