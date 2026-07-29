#!/usr/bin/env bash
#
# SPDX-License-Identifier: GPL-2.0
#
# This file is a part of the Armbian Build Framework
# https://github.com/armbian/build/
#
# Turns the built .img into an eMMC installer package for the Milk-V Duo S.
#
# The Duo S cannot install itself. Armbian's usual "boot from SD, run
# armbian-install, copy to eMMC" route is not available, because on this board
# the microSD slot and the eMMC are mutually exclusive - inserting a card
# disconnects the eMMC - so a running system never sees both at once. The eMMC
# has to be written by something that is not Linux.
#
# That something is the vendor bootloader. The Sophgo U-Boot carries a
# 'cvi_update' command (cmd/cvi_update.c) which reads files out of the FAT root
# of the SD card and writes them to the raw eMMC. It runs before Linux, so the
# card/eMMC exclusivity does not apply: in U-Boot the card is mmc 1 and the eMMC
# is mmc 0. See the DTS comments in
# packages/sophgo-sg200x/u-boot/*/dts/*_emmc.dts for how that numbering arises.
#
# What makes it fire is not a file on the card but how the bootloader was built.
# cvitek.mk turns STORAGE_TYPE=emmc into -DCONFIG_EMMC_SUPPORT, and
# cv181x-asic.h then sets
#
#     CONFIG_BOOTCOMMAND "cvi_update || run distro_bootcmd || ..."
#
# where the SD build gets "run distro_bootcmd || run sdboot" instead. The FSBL,
# for its part, writes MAGIC_NUM_SD_DL to SRAM on *every* boot whose source was
# the SD card (setup_dl_flag() in plat/cv181x/platform.c), which is what
# cvi_update checks. So an -emmc bootloader plus any SD boot means: flash.
#
# The installer and the installed system therefore share one fip.bin - the same
# file cvi_update copies into the eMMC boot hardware partition. On a later boot
# with no card, the boot source is the eMMC, the magic does not match, cvi_update
# returns non-zero and the '||' falls through to distro_bootcmd, which finds
# /extlinux/extlinux.conf on the eMMC. With a card present the BootROM prefers
# the card and the whole thing runs again - which is exactly why the install
# procedure insists on removing it.
#
# The payload is the entire Armbian disk image, MBR and all, written at offset 0.
# _prgImage() writes each chunk to an absolute byte offset, so nothing forces us
# into the vendor's BOOT/MISC/ENV/ROOTFS split - and staying out of it is what
# keeps extlinux, apt kernel upgrades and first-boot resize working. The vendor
# layout has no filesystem on /boot at all, just a raw FIT image.
#
# ARM vs RISC-V. Only the RISC-V side has been installed on real hardware, and
# the vendor has never shipped an ARM eMMC image either - their package is the
# cv1813h_milkv_duos_emmc project and its boot.emmc FIT says arch = "riscv". So
# the ARM path is reasoned rather than observed. What was checked:
#
#   - setup_dl_flag() is in the shared bl2 (plat/cv181x/bl2/bl2_main.c) and runs
#     unconditionally; CONFIG_CMD_CVI_UPDATE is "default y" with no arch
#     dependency; BOOT_SOURCE_FLAG_ADDR comes from board/cvitek/cv181x/, which
#     both arches share; cvi_board_memmap.h is byte-identical between them.
#   - the mmc numbering is confirmed on ARM hardware. There are no mmc aliases
#     in any of the DTS, so U-Boot numbers by node order, and an ARM SD boot
#     prints "MMC: cv-sd@4310000: 0, wifi-sd@4320000: 1" - cv-emmc@4300000
#     missing only because cv181x_asic_sd.dtsi deletes it. Keeping it puts it
#     first, which is the split cvi_update assumes.
#   - the per-arch base dtsi re-open cv-emmc/cv-sd only to patch interrupts
#     (GIC_SPI against PLIC), which does not move them in the tree.
#
# What is inferred: that the boot-source flag survives bl31, where RISC-V has
# OpenSBI instead. AXI_SRAM_SIZE is 0x40 - a 64-byte mailbox with allocated
# slots, one of which (ATF_DBG_REG, +0xC) belongs to ATF, so ATF knows the
# layout - and CVIMMAP_MONITOR_ADDR is 0x80000000, so bl31 runs from DRAM and
# cannot clobber it in passing.
#
# If that inference is wrong the result is benign: cvi_update returns non-zero,
# the '||' falls through to distro_bootcmd and nothing is written to the eMMC.
# It fails to install rather than installing badly.
#
# Installation, for the record:
#
#   1. format an SD card as FAT32
#   2. unzip this package into its root
#   3. insert the card and power on; watch the serial console
#   4. when it finishes, power off
#   5. remove the card - otherwise it flashes again on the next boot
#   6. power on
#

function extension_prepare_config__sophgo_emmc_installer() {
	# Enabled by the -emmc board files, which are also the only thing that sets
	# SOPHGO_CVI_STORAGE. Enabling it by hand on an SD board would produce a
	# package whose fip.bin never runs cvi_update, so it would sit there doing
	# nothing; say so rather than shipping it.
	if [[ "${SOPHGO_CVI_STORAGE}" != "emmc" ]]; then
		exit_with_error "${EXTENSION} needs SOPHGO_CVI_STORAGE=emmc" \
			"BOARD=${BOARD} builds a '${SOPHGO_CVI_STORAGE}' bootloader; use a -emmc board"
	fi

	# The payload is inside a zip already. Letting output_images_compress_and_checksum
	# put xz or zstd around it would spend minutes to gain nothing. Keep the
	# checksum if one was asked for, drop the rest.
	#
	# This hook runs from do_main_configuration(), which is well before
	# config-prepare.sh substitutes the "sha,img" default for an empty or "no"
	# value - so an unset COMPRESS_OUTPUTIMAGE here still means the default is
	# coming, and testing it as-is would read as "no sha wanted" and throw the
	# checksum away. Apply the same default first, then narrow it.
	declare compress="${COMPRESS_OUTPUTIMAGE}"
	[[ -z "${compress}" || "${compress}" == "no" ]] && compress="sha,img"
	if [[ "${compress}" == *sha* ]]; then
		declare -g COMPRESS_OUTPUTIMAGE="sha"
	else
		declare -g COMPRESS_OUTPUTIMAGE="none"
	fi

	display_alert "${EXTENSION}" "eMMC installer package for ${BOARD}" "info"
}

function add_host_dependencies__sophgo_emmc_installer() {
	# zip for the package itself; mtools for mcopy, which reads fip.bin out of
	# the image's FAT partition without needing a loop device or root.
	declare -g EXTRA_BUILD_DEPS="${EXTRA_BUILD_DEPS} zip mtools"
}

function post_build_image__900_sophgo_emmc_installer() {
	[[ -z "${version}" ]] && exit_with_error "version is not set"

	declare image_file="${DESTIMG}/${version}.img"
	[[ -f "${image_file}" ]] || exit_with_error "image not found" "${image_file}"

	declare mkcimg="${SRC}/packages/sophgo-sg200x/tools/mkcimg.py"
	[[ -f "${mkcimg}" ]] || exit_with_error "mkcimg.py not found" "${mkcimg}"

	# Normally emitted by build_image_from_rootfs() right after this hook, but
	# only when ${version}.img still exists - and it will not, because the
	# installer replaces it below. Write it here so the package manifest
	# survives; it describes the system that ends up on the eMMC either way.
	fingerprint_image "${DESTIMG}/${version}.img.txt" "${version}"

	# The staging directory becomes the FAT root of the installer card, so the
	# layout here is literally what the user unzips. It lives in DESTIMG because
	# the payload is image-sized and /tmp is often tmpfs, but deliberately
	# without the ${version} prefix: output_images_compress_and_checksum and
	# move_images_to_final_destination both glob "${version}*", and a directory
	# left behind by a failed build would land in their way.
	declare stage="${DESTIMG}/emmc-installer-stage"
	run_host_command_logged rm -rf "${stage}"
	run_host_command_logged mkdir -p "${stage}"

	# fip.bin is taken out of the image's own FAT partition, where
	# post_write_uboot_platform__sophgo_sg200x_install_fip put it, rather than
	# hunted down in output/debs. Several u-boot .debs for one BOARD/BRANCH can
	# coexist there - one per artifact hash - and picking among them by name
	# risks shipping a bootloader from an earlier configuration.
	#
	# Taking it from the image also guarantees the property that matters here:
	# the fip.bin cvi_update writes into the eMMC boot hardware partition is the
	# same file as the /boot/fip.bin inside the payload it writes next to it. If
	# those two ever drifted apart the board would boot one and show the other.
	declare boot_offset
	boot_offset="$(sfdisk -J "${image_file}" | python3 -c \
		'import json,sys; print(json.load(sys.stdin)["partitiontable"]["partitions"][0]["start"] * 512)')"
	[[ "${boot_offset}" =~ ^[0-9]+$ ]] || exit_with_error "could not read partition 1 offset" "${image_file}"

	display_alert "${EXTENSION}" "extracting fip.bin from /boot (partition 1 at ${boot_offset})" "info"
	run_host_command_logged mcopy -n -i "${image_file}@@${boot_offset}" ::/fip.bin "${stage}/fip.bin"
	[[ -s "${stage}/fip.bin" ]] || exit_with_error "no fip.bin in the image's boot partition" "${image_file}"

	# armbian.emmc: the name cvi_update looks for, from imgs.h in
	# packages/sophgo-sg200x/u-boot/*/include/emmc/. Offset 0 - the image starts
	# with its own MBR and owns the whole device.
	display_alert "${EXTENSION}" "wrapping $(( $(stat -c %s "${image_file}") / 1024 / 1024 ))MB image in CIMG" "info"
	run_host_command_logged python3 "${mkcimg}" \
		--offset 0 --label ROOTFS \
		"${image_file}" "${stage}/armbian.emmc"

	# Drop the raw .img now that the payload carries it, rather than after the
	# zip: it saves holding three copies of an image-sized file at once, and
	# nothing below reads it again. It is not a usable SD image for this board
	# anyway - booting it would run cvi_update, which finds fip.bin on the card,
	# rewrites the eMMC bootloader with it, finds no armbian.emmc, and *still*
	# returns success, so the '||' never reaches distro_bootcmd and the board
	# does not boot at all. Removing it also means the 'if [[ -f ...img ]]' block
	# after this hook is skipped, so a stray CARD_DEVICE cannot write it either.
	display_alert "${EXTENSION}" "discarding the raw .img; the installer is the deliverable" "info"
	run_host_command_logged rm -f "${image_file}"

	declare zip_file="${DESTIMG}/${version}.emmc-installer.zip"
	display_alert "${EXTENSION}" "packing $(basename "${zip_file}")" "info"
	# -j so the files land in the FAT root when unzipped, which is where
	# cvi_update looks; no path prefix. Compression is left at zip's default -6:
	# on a 1764MB payload that measured 492MB in 87s against 524MB in 43s at -1,
	# and 44 seconds inside a build this long is worth 32MB on every download.
	# The ratio is this good because the image's free space is already zeroes.
	run_host_command_logged zip -j "${zip_file}" \
		"${stage}/fip.bin" "${stage}/armbian.emmc"
	rm -rf "${stage}"

	[[ -f "${zip_file}" ]] || exit_with_error "zip did not produce" "${zip_file}"

	display_alert "${EXTENSION}" \
		"$(basename "${zip_file}") is $(( $(stat -c %s "${zip_file}") / 1024 / 1024 ))MB" "info"
	display_alert "${EXTENSION}" \
		"unzip to a FAT32 card, boot once, power off, remove the card" "info"

	return 0
}
