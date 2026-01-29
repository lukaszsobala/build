#
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) Authors: https://armbian.com/authors
#
# Sophgo/Milk-V AIC8800 WiFi/Bluetooth driver extension
#
# This extension installs the AIC8800 WiFi 6 / Bluetooth 5 driver for Sophgo-based
# boards like the Milk-V Duo S. The AIC8800 chip (AIC8800D80) is connected via SDIO.
#
# The driver source is from Aicsemi (the chip vendor) and is packaged by Radxa.
# This extension uses Radxa's DKMS packages but configures firmware paths and
# initialization scripts specific to Sophgo/Milk-V boards.
#
# Supported architectures:
#   - ARM64 (Cortex-A53 mode)
#   - RISC-V 64-bit (C906 mode)
#
# The DKMS package compiles the kernel module for the target architecture
# during image build, so this extension works for both ARM64 and RISC-V builds.
#
# Usage in board config:
#   enable_extension "sophgo-aic8800"
#
# Reference:
# - Milk-V Duo S: https://milkv.io/docs/duo/getting-started/duos
# - Radxa AIC8800: https://github.com/radxa-pkg/aic8800
#

function extension_finish_config__install_kernel_headers_for_sophgo_aic8800_dkms() {
	if [[ "${KERNEL_HAS_WORKING_HEADERS}" != "yes" ]]; then
		display_alert "Kernel version has no working headers package" "skipping aic8800 dkms for kernel v${KERNEL_MAJOR_MINOR}" "warn"
		return 0
	fi
	declare -g INSTALL_HEADERS="yes"
	display_alert "Forcing INSTALL_HEADERS=yes; for use with Sophgo aic8800 dkms" "${EXTENSION}" "debug"
}

function post_install_kernel_debs__install_sophgo_aic8800_dkms_package() {
	# Check kernel version compatibility
	if linux-version compare "${KERNEL_MAJOR_MINOR}" ge 6.20; then
		display_alert "Kernel version is too recent" "skipping aic8800 dkms for kernel v${KERNEL_MAJOR_MINOR}" "warn"
		return 0
	fi

	[[ "${INSTALL_HEADERS}" != "yes" ]] || [[ "${KERNEL_HAS_WORKING_HEADERS}" != "yes" ]] && return 0

	# Get latest version from Radxa's repo (same Aicsemi driver)
	local api_url="https://api.github.com/repos/radxa-pkg/aic8800/releases/latest"
	local latest_version
	latest_version=$(curl -s "${api_url}" | jq -r '.tag_name')

	if [[ -z "${latest_version}" ]] || [[ "${latest_version}" == "null" ]]; then
		display_alert "Failed to get AIC8800 version from GitHub API" "${EXTENSION}" "warn"
		return 1
	fi

	display_alert "Installing AIC8800 WiFi/BT driver" "version ${latest_version}" "info"

	# URLs for SDIO driver (Sophgo boards use SDIO interface)
	local aic8800_firmware_url="https://github.com/radxa-pkg/aic8800/releases/download/${latest_version}/aic8800-firmware_${latest_version}_all.deb"
	local aic8800_sdio_url="https://github.com/radxa-pkg/aic8800/releases/download/${latest_version}/aic8800-sdio-dkms_${latest_version}_all.deb"

	# Handle GitHub mirror for China
	if [[ "${GITHUB_MIRROR}" == "ghproxy" ]]; then
		local ghproxy_header="https://ghfast.top/"
		aic8800_firmware_url="${ghproxy_header}${aic8800_firmware_url}"
		aic8800_sdio_url="${ghproxy_header}${aic8800_sdio_url}"
	fi

	# Download packages
	use_clean_environment="yes" chroot_sdcard "wget -q ${aic8800_sdio_url} -O /tmp/aic8800-sdio-dkms.deb" || {
		display_alert "Failed to download AIC8800 SDIO driver" "${EXTENSION}" "warn"
		return 1
	}

	use_clean_environment="yes" chroot_sdcard "wget -q ${aic8800_firmware_url} -O /tmp/aic8800-firmware.deb" || {
		display_alert "Failed to download AIC8800 firmware" "${EXTENSION}" "warn"
		return 1
	}

	# Install packages (will build kernel module in chroot)
	display_alert "Building AIC8800 kernel module in chroot" "${EXTENSION}" "info"
	declare -ag if_error_find_files_sdcard=("/var/lib/dkms/aic8800*/*/build/*.log")
	use_clean_environment="yes" chroot_sdcard_apt_get_install "/tmp/aic8800-sdio-dkms.deb /tmp/aic8800-firmware.deb"

	# Cleanup downloaded debs
	use_clean_environment="yes" chroot_sdcard "rm -f /tmp/aic8800*.deb"

	# Create Sophgo-specific firmware directory structure
	# Milk-V SDK uses /mnt/system/firmware/aic8800/ but on Armbian we use standard paths
	# The Radxa firmware package installs to /lib/firmware/aic8800/ which is standard
	display_alert "Configuring AIC8800 for Sophgo" "${EXTENSION}" "info"

	# Create systemd network link for consistent wlan naming
	use_clean_environment="yes" chroot_sdcard "mkdir -p /usr/lib/systemd/network/"
	use_clean_environment="yes" chroot_sdcard 'cat <<- EOF > /usr/lib/systemd/network/50-sophgo-aic8800.link
		[Match]
		OriginalName=wlan*
		Driver=aic8800*

		[Link]
		NamePolicy=kernel
	EOF'

	# Create modprobe configuration for Sophgo boards
	use_clean_environment="yes" chroot_sdcard "mkdir -p /etc/modprobe.d/"
	use_clean_environment="yes" chroot_sdcard 'cat <<- EOF > /etc/modprobe.d/sophgo-aic8800.conf
		# AIC8800 WiFi/BT configuration for Sophgo/Milk-V boards
		# The chip is connected via SDIO interface

		# Load aic8800_fdrv after aic8800_bsp
		softdep aic8800_fdrv pre: aic8800_bsp

		# Optional: Set custom MAC address (uncomment and modify)
		# options aic8800_fdrv mac_addr=00:11:22:33:44:55
	EOF'

	# Create udev rules for proper module loading
	use_clean_environment="yes" chroot_sdcard "mkdir -p /etc/udev/rules.d/"
	use_clean_environment="yes" chroot_sdcard 'cat <<- EOF > /etc/udev/rules.d/99-sophgo-aic8800.rules
		# AIC8800 WiFi/BT udev rules for Sophgo/Milk-V boards
		# Ensure proper permissions and module loading

		# AIC8800 SDIO device
		ACTION=="add", SUBSYSTEM=="sdio", ATTR{device}=="0xa800", RUN+="/sbin/modprobe aic8800_fdrv"
	EOF'

	# Create helper script for WiFi configuration (compatible with Milk-V approach)
	use_clean_environment="yes" chroot_sdcard "mkdir -p /usr/local/bin/"
	use_clean_environment="yes" chroot_sdcard 'cat <<- "EOFSCRIPT" > /usr/local/bin/sophgo-wifi-setup
#!/bin/bash
# Sophgo/Milk-V WiFi setup helper script
# This provides similar functionality to the Milk-V SDK auto.sh

INTERFACE="wlan0"
MAX_ATTEMPTS=30
LOG_FILE="/var/log/sophgo-wifi.log"

log() {
    echo "$(date "+%Y-%m-%d %H:%M:%S") $*" >> "$LOG_FILE"
}

wait_for_interface() {
    local attempt=0
    while [ $attempt -lt $MAX_ATTEMPTS ]; do
        if ip link show "$INTERFACE" > /dev/null 2>&1; then
            log "Interface $INTERFACE is available"
            return 0
        fi
        log "Waiting for $INTERFACE... (attempt $((attempt+1))/$MAX_ATTEMPTS)"
        sleep 1
        attempt=$((attempt + 1))
    done
    log "Interface $INTERFACE not found after $MAX_ATTEMPTS attempts"
    return 1
}

set_mac_address() {
    local mac_file="/etc/sophgo-wifi-mac"
    if [ -f "$mac_file" ]; then
        local mac_addr
        mac_addr=$(cat "$mac_file")
        if [ -n "$mac_addr" ]; then
            log "Setting MAC address to $mac_addr"
            ip link set "$INTERFACE" down
            ip link set "$INTERFACE" address "$mac_addr"
            ip link set "$INTERFACE" up
        fi
    fi
}

case "$1" in
    wait)
        wait_for_interface
        ;;
    mac)
        set_mac_address
        ;;
    *)
        echo "Usage: $0 {wait|mac}"
        echo "  wait - Wait for WiFi interface to appear"
        echo "  mac  - Set custom MAC address from /etc/sophgo-wifi-mac"
        exit 1
        ;;
esac
EOFSCRIPT'
	use_clean_environment="yes" chroot_sdcard "chmod +x /usr/local/bin/sophgo-wifi-setup"

	display_alert "AIC8800 WiFi/BT driver installed for Sophgo" "${EXTENSION}" "info"
}

# Optional: Add modules to initramfs if needed early in boot
function extension_prepare_config__sophgo_aic8800_modules() {
	# AIC8800 is typically not needed in initramfs, but ensure modules are available
	display_alert "Sophgo AIC8800 extension loaded" "${EXTENSION}" "debug"
}
