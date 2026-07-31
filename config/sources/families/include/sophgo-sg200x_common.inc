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
# (falling back to partition 1 when none are), and picks up /boot.scr from the
# FAT partition - scan_dev_for_boot looks for /extlinux/extlinux.conf first and
# we no longer write one, so it falls through to the script.
#

# /boot is a FAT partition because the BootROM has to be able to read fip.bin
# out of it. 128MiB matches the vendor image and leaves room for a few kernels.
declare -g BOOTFS_TYPE="fat"
declare -g BOOTSIZE="128"

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

	# Stamp which of the two bootloaders this is, next to the file it describes.
	# Nothing at boot reads it; sophgo-emmc-install does, to refuse putting an SD
	# fip.bin into the eMMC boot partition - that one has cv-emmc@4300000 deleted
	# from its device tree, so it comes up with no eMMC and nothing to boot. The
	# two cannot be told apart from the file: U-Boot is LZMA-compressed inside
	# fip.bin, so not even the CONFIG_BOOTCOMMAND string survives to grep for.
	display_alert "Sophgo SG200x" "stamping /boot/sophgo-storage.txt as '${SOPHGO_CVI_STORAGE}'" "info"
	echo "${SOPHGO_CVI_STORAGE}" > "${MOUNT}/boot/sophgo-storage.txt"
}

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
# a different reason; extensions/sophgo-aic8800.sh installs the rule for that and
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
