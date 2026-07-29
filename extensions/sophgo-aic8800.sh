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

# Firmware comes from armbian-firmware, which every image installs; this repo
# ships none. It used to carry Milk-V's own aicsemi build, a different one to
# upstream's - but upstream's drives this chip just as well (verified on a Duo S:
# 5 GHz association, iperf3 clean, no firmware errors), so the blobs are gone and
# this is simply the path every other AIC8800 board already uses.
#
# One variable because the path reaches the modules two different ways; see
# custom_kernel_config below. It is also overridable at runtime, which is the easy
# way to test a different firmware set without rebuilding:
#   modprobe aic8800_bsp aic_fw_path=/some/other/dir
declare -g AIC8800_FW_DIR="/lib/firmware/aic8800/SDIO/aic8800D80"

function post_family_config__sophgo_aic8800_fetch() {
	fetch_from_repo "${AIC8800_REPO}" "aic8800-milkv-duos" "${AIC8800_REF}" "yes"
	declare -g AIC8800_SRC_DIR="${SRC}/cache/sources/aic8800-milkv-duos/${AIC8800_REF#*:}"
}

function custom_kernel_config__sophgo_aic8800_modules() {
	declare -a patches=()
	mapfile -t patches < <(find "${SRC}/patch/misc/sophgo-aic8800" -maxdepth 1 -name '*.patch' | sort)

	# Rebuild the kernel when the driver revision or the patches change.
	kernel_config_modifying_hashes+=("sophgo_aic8800=${AIC8800_REF}")
	kernel_config_modifying_hashes+=("sophgo_aic8800_patches=$(cat /dev/null "${patches[@]}" | sha256sum | cut -d' ' -f1)")

	# Also called during config dumping / version calculation, with no kernel tree.
	[[ ! -f .config ]] && return 0

	declare wireless_dir="${kernel_work_dir}/drivers/net/wireless"
	declare driver_dir="${wireless_dir}/aicsemi"

	display_alert "Sophgo AIC8800" "adding driver to kernel tree" "info"

	run_host_command_logged rm -rf "${driver_dir}"
	run_host_command_logged mkdir -p "${driver_dir}"
	run_host_command_logged cp -a "${AIC8800_SRC_DIR}/aicsemi/." "${driver_dir}/"

	# The vendor sources are not maintained against new kernels; the patches
	# here are what keeps them building. They are applied to the copy in the
	# kernel tree, not to the shared cache/sources checkout.
	declare patch_file
	for patch_file in "${patches[@]}"; do
		display_alert "Sophgo AIC8800" "applying $(basename "${patch_file}")" "info"
		run_host_command_logged patch --batch -p1 -d "${kernel_work_dir}" "<" "${patch_file}" ||
			exit_with_error "AIC8800 patch did not apply" "$(basename "${patch_file}")"
	done

	# The firmware path reaches the two modules by two different routes:
	# aic8800_bsp/Makefile assigns CONFIG_AIC_FW_PATH itself (with =, not ?=), so it
	# overrides the Kconfig value for its own objects, while aic8800_fdrv/Makefile
	# takes whatever .config says. Set both, or the modules end up looking in two
	# different directories.
	sed -i "s|^CONFIG_AIC_FW_PATH = .*|CONFIG_AIC_FW_PATH = \"${AIC8800_FW_DIR}\"|" \
		"${driver_dir}/aic8800/aic8800_bsp/Makefile"
	grep -qF "CONFIG_AIC_FW_PATH = \"${AIC8800_FW_DIR}\"" "${driver_dir}/aic8800/aic8800_bsp/Makefile" ||
		exit_with_error "AIC8800 firmware path not set in the vendor Makefile" "${AIC8800_FW_DIR}"

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
	kernel_config_set_string CONFIG_AIC_FW_PATH "${AIC8800_FW_DIR}"

	# cfg80211 has to be reachable from the driver.
	kernel_config_set_m CONFIG_CFG80211
}

# armbian-firmware provides the blobs and is installed well before this hook runs
# (lib/functions/rootfs/distro-agnostic.sh). Nothing to do here but make sure they
# are actually present: the driver opens them by absolute path with no fallback,
# and when one is missing it retries forever, power-cycling the chip about every
# 1.7s. That is what shipped in the 2026-07-28 arm64 test image, and it is a
# miserable thing to debug from the far end - so fail the build instead.
# INSTALL_ARMBIAN_FIRMWARE=no, or armbian/firmware reorganising the directory, are
# the ways it can go missing.
function post_family_tweaks__sophgo_aic8800_firmware() {
	declare fw_dir="${SDCARD}${AIC8800_FW_DIR}"

	display_alert "Sophgo AIC8800" "checking firmware from armbian-firmware" "info"

	# aic8800_bsp uploads the first four - fw_adid/fw_patch/fw_patch_table are the
	# Bluetooth patch it pushes over SDIO, fmacfw is the Wi-Fi MAC firmware - and
	# aic8800_fdrv reads the userconfig. lmacfw_rf is deliberately not checked: it
	# is only reachable by putting the bsp into RF test mode via its cpmode sysfs
	# attribute, so a missing one does not stop the board working.
	declare fw_file
	for fw_file in fw_patch_table_8800d80_u02.bin fw_adid_8800d80_u02.bin \
		fw_patch_8800d80_u02.bin fmacfw_8800d80_u02.bin aic_userconfig_8800d80.txt; do
		[[ -f "${fw_dir}/${fw_file}" ]] ||
			exit_with_error "AIC8800 firmware missing from the rootfs" "${fw_dir}/${fw_file}"
	done
}

# Anything written into the rootfs has to be written from here or earlier, not from
# pre_umount_final_image: ${SDCARD} is rsynced into the mounted image early in
# rootfs-to-image.sh, and update_initramfs follows immediately. Writes from the
# later hook still "succeed" - ${SDCARD} is a live host path - but never reach the
# image.
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
