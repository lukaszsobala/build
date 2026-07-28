#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2013-2026 Igor Pecovnik, igor@armbian.com
#
# This file is a part of the Armbian Build Framework
# https://github.com/armbian/build/
#
# Shared image layout for the Sophgo SG200x families (ARM and RISC-V modes).
#
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
# (falling back to partition 1 when none are), and picks up
# /extlinux/extlinux.conf from the FAT partition.
#

# /boot is a FAT partition because the BootROM has to be able to read fip.bin
# out of it. 128MiB matches the vendor image and leaves room for a few kernels.
declare -g BOOTFS_TYPE="fat"
declare -g BOOTSIZE="128"

function pre_prepare_partitions__sophgo_sg200x_msdos_layout() {
	# The BootROM parses an MBR; riscv64.conf and arm64.conf both default to GPT.
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
}
