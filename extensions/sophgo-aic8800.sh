#!/usr/bin/env bash
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2013-2026 Igor Pecovnik, igor@armbian.com
#
# This file is a part of the Armbian Build Framework
# https://github.com/armbian/build/
#
# Extension: sophgo-aic8800
#
# AIC8800D80 Wi-Fi 6 + Bluetooth 5 support for Sophgo SG200x boards (Milk-V
# Duo S), where the chip hangs off SDIO (sdhci1) and its enable line is a pin
# that has to be poked directly - there is no mainline driver.
#
# The sources are the vendor driver out of Milk-V's duo-buildroot-sdk-v2, as
# cleaned up for modern kernels by queenkjuul. They are copied into the kernel
# tree and built as ordinary in-tree modules, which is much cheaper than DKMS
# under qemu and needs no headers package on the target.
#
# The copy happens from custom_kernel_config, not kernel_copy_extra_sources:
# patching does a git reset --hard plus a clean of untracked files, so anything
# staged earlier than that gets wiped.
#

declare -g AIC8800_REPO="https://github.com/queenkjuul/aic8800-milkv-duos"
# Pinned; a mutable branch ref would break kernel build caching.
declare -g AIC8800_REF="commit:ccf8fd059f70384fae4878c1048603510c2df700"

function post_family_config__sophgo_aic8800_fetch() {
	fetch_from_repo "${AIC8800_REPO}" "aic8800-milkv-duos" "${AIC8800_REF}" "yes"
	declare -g AIC8800_SRC_DIR="${SRC}/cache/sources/aic8800-milkv-duos/${AIC8800_REF#*:}"
}

function custom_kernel_config__sophgo_aic8800_modules() {
	# Rebuild the kernel when the driver revision changes.
	kernel_config_modifying_hashes+=("sophgo_aic8800=${AIC8800_REF}")

	# Also called during config dumping / version calculation, with no kernel tree.
	[[ ! -f .config ]] && return 0

	declare wireless_dir="${kernel_work_dir}/drivers/net/wireless"
	declare driver_dir="${wireless_dir}/aicsemi"

	display_alert "Sophgo AIC8800" "adding driver to kernel tree" "info"

	run_host_command_logged rm -rf "${driver_dir}"
	run_host_command_logged mkdir -p "${driver_dir}"
	run_host_command_logged cp -a "${AIC8800_SRC_DIR}/aicsemi/." "${driver_dir}/"

	# Hook the new vendor directory into the wireless Kconfig and Makefile.
	if ! grep -q "aicsemi/Kconfig" "${wireless_dir}/Kconfig"; then
		sed -i 's|^source "drivers/net/wireless/admtek/Kconfig"|source "drivers/net/wireless/aicsemi/Kconfig"\n&|' \
			"${wireless_dir}/Kconfig"
	fi
	if ! grep -q "aicsemi/" "${wireless_dir}/Makefile"; then
		echo 'obj-$(CONFIG_WLAN_VENDOR_AICSEMI) += aicsemi/' >> "${wireless_dir}/Makefile"
	fi

	# CONFIG_AIC8800 is a bool that only makes the build descend into aic8800/;
	# the Makefile in there forces the three actual modules to =m.
	kernel_config_set_y CONFIG_WLAN_VENDOR_AICSEMI
	kernel_config_set_y CONFIG_AIC8800
	kernel_config_set_string CONFIG_AIC_FW_PATH "/lib/firmware/aic8800"

	# cfg80211 has to be reachable from the driver.
	kernel_config_set_m CONFIG_CFG80211
}

# Deliberately post_family_tweaks and not pre_umount_final_image: the rootfs is
# rsynced from ${SDCARD} to the mounted image early in rootfs-to-image.sh, and
# update_initramfs runs right after that - both well before pre_umount_final_image
# fires. Writing to ${SDCARD} from that later hook still "succeeds", because the
# directory is a live host path, but the files never reach the image. That is
# exactly what shipped in the 2026-07-28 arm64 test image: the driver looped
# forever on "fw_patch_table_8800d80_u02.bin file failed to open", power-cycling
# the chip every ~1.7s. Keep this copy on the ${SDCARD} side of the rsync.
function post_family_tweaks__sophgo_aic8800_firmware() {
	declare fw_src="${SRC}/packages/blobs/sophgo/milkv-duos/aic8800-firmware"
	declare fw_dst="${SDCARD}/lib/firmware/aic8800"

	display_alert "Sophgo AIC8800" "installing firmware blobs" "info"
	run_host_command_logged mkdir -p "${fw_dst}"
	run_host_command_logged cp -v "${fw_src}"/* "${fw_dst}/"

	# The driver opens CONFIG_AIC_FW_PATH/fw_patch_table_8800d80_u02.bin by
	# absolute path and has no fallback; if it is missing the retry loop above
	# is what the user sees. Fail the build here instead.
	[[ -f "${fw_dst}/fw_patch_table_8800d80_u02.bin" ]] ||
		exit_with_error "AIC8800 firmware did not land in the rootfs" "${fw_dst}"
}

function post_family_tweaks__sophgo_aic8800_modprobe() {
	display_alert "Sophgo AIC8800" "configuring module load order" "info"

	mkdir -p "${SDCARD}/etc/modprobe.d"
	cat <<- 'EOF' > "${SDCARD}/etc/modprobe.d/sophgo-aic8800.conf"
		# aic8800_bsp brings the chip out of reset and uploads firmware; the
		# wifi and bluetooth drivers are useless until it has run.
		softdep aic8800_fdrv pre: aic8800_bsp
		softdep aic8800_btlpm pre: aic8800_bsp
	EOF

	mkdir -p "${SDCARD}/etc/modules-load.d"
	cat <<- 'EOF' > "${SDCARD}/etc/modules-load.d/sophgo-aic8800.conf"
		aic8800_bsp
		aic8800_fdrv
	EOF
}
