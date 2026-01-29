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

# Hook to write fip.bin to the image before unmounting
# This is called for BOOTCONFIG="none" boards where write_uboot_platform is not invoked
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

	display_alert "Writing Sophgo fip.bin" "${fip_blob} -> ${LOOP}" "info"

	# Write fip.bin starting at sector 1 (512 bytes offset, skip MBR at sector 0)
	# This matches the Sophgo/Milkv SDK layout
	dd if="${fip_blob}" of="${LOOP}" bs=512 seek=1 conv=notrunc,fsync status=none

	display_alert "Sophgo bootloader" "Written successfully to ${LOOP}" "info"
}
