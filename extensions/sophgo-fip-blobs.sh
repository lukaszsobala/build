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
# Installs the Sophgo SG200x bootloader blob (fip.bin) into the boot partition.
#
# fip.bin bundles the FSBL, OpenSBI (RISC-V only) and U-Boot 2021.10. The
# BootROM looks for it by name in the first FAT partition of the card, so it is
# copied in as an ordinary file rather than written to raw sectors.
#
# The blob has to be one with distroboot enabled, otherwise U-Boot never looks
# for /extlinux/extlinux.conf and the image will not boot. See
# packages/blobs/sophgo/milkv-duos/README.md for where the blobs come from and
# how to build one.
#
# Set SOPHGO_CPU_OVERDRIVE=yes to pick the 1050MHz variant over the 850MHz
# vendor default, where both are shipped.
#

function extension_prepare_config__sophgo_fip_blobs() {
	declare -g SOPHGO_CPU_OVERDRIVE="${SOPHGO_CPU_OVERDRIVE:-no}"

	# Board configs are named <board>-arm / <board>-riscv, one blob dir serves both.
	declare board_base="${BOARD%-arm}"
	board_base="${board_base%-riscv}"
	declare -g SOPHGO_FIP_BLOB_DIR="${SRC}/packages/blobs/sophgo/${board_base}"
}

# Echoes the path of the blob to use, or returns 1 if there is none.
function sophgo_find_fip_blob() {
	declare suffix=""
	[[ "${SOPHGO_CPU_OVERDRIVE}" == "yes" ]] && suffix="-od"

	declare candidate
	for candidate in \
		"${SOPHGO_FIP_BLOB_DIR}/fip-${ARCH}${suffix}.bin" \
		"${SOPHGO_FIP_BLOB_DIR}/fip-${ARCH}.bin" \
		"${SOPHGO_FIP_BLOB_DIR}/fip${suffix}.bin" \
		"${SOPHGO_FIP_BLOB_DIR}/fip.bin"; do
		if [[ -f "${candidate}" ]]; then
			echo "${candidate}"
			return 0
		fi
	done

	return 1
}

function pre_umount_final_image__write_sophgo_fip_blob() {
	# Families that build their own bootloader install it from the u-boot
	# package instead; see sophgo-sg200x_common.inc.
	if [[ "${BOOTCONFIG}" != "none" ]]; then
		display_alert "Sophgo fip.bin" "built from source, not using a prebuilt blob" "info"
		return 0
	fi

	declare fip_blob
	fip_blob="$(sophgo_find_fip_blob)" || true

	if [[ -z "${fip_blob}" ]]; then
		display_alert "No Sophgo fip.bin found for ${BOARD}" "the image will NOT boot as-is" "wrn"
		display_alert "Expected" "${SOPHGO_FIP_BLOB_DIR}/fip-${ARCH}.bin" "wrn"
		display_alert "See" "packages/blobs/sophgo/milkv-duos/README.md" "info"
		return 0
	fi

	display_alert "Installing Sophgo bootloader" "$(basename "${fip_blob}") -> /boot/fip.bin" "info"
	run_host_command_logged cp -v "${fip_blob}" "${MOUNT}/boot/fip.bin"
}
