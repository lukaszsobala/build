# Milk-V Duo S (Sophgo SG2000) - RISC-V mode
# T-Head C906 @ 1GHz, 512MB LPDDR3, microSD, 100M Ethernet, USB-C OTG, USB-A,
# AIC8800D80 Wi-Fi 6 + BT 5, 26-pin + 14-pin GPIO headers.
#
# The SG2000 has both a C906 RISC-V core and a Cortex-A53, and only one of them
# runs at a time. Which one is chosen by the slide switch on the board, which is
# what brings that core out of reset and runs the BootROM on it.
#
# So writing this image is only half of it: the switch has to be in the RISC-V
# position as well. fip.bin is built for one architecture and says so in its TOC
# header (0xC906B001 here, 0xAA640001 for ARM), and the BootROM will not load
# the wrong one - a mismatch does not boot at all, with no message to say why.
# The first line of the boot log is the quickest way to tell which core you are
# actually on: it starts with 'C' for RISC-V and 'B' for ARM.
#
# Two board files for one physical board, because BOARD is the build target and
# each produces a different image. They stay deliberately thin - everything the
# two share (zram tuning, SERIALCON, kernel branches, bootloader, boot script)
# is in the family, which is why only BOARDFAMILY below differs from
# milkv-duos-arm. The bootloader is built from source; see
# config/sources/families/include/sophgo-sg200x_common.inc.
#
# To run from the eMMC instead of the card, build this same board with
#
#   ./compile.sh build BOARD=milkv-duos-riscv BRANCH=edge SOPHGO_CVI_STORAGE=emmc
#
# A build switch rather than a board of its own, because the two differ only in
# how the bootloader is configured. What comes out is an ordinary bootable
# image; write it with sophgo-emmc-install from a running SD system, which also
# puts fip.bin into the eMMC hardware boot partition - the one part of the eMMC
# a disk image cannot describe, and without which the board will not boot.
# ENABLE_EXTENSIONS=image-output-sophgo-emmc-installer turns that image into a
# self-flashing card instead, for when there is no system to install from.
#
# There is deliberately no SRC_CMDLINE. It is read only on the extlinux path and
# when the boot script is a .template, and this family uses neither: console,
# earlycon, verbosity and root device all come from /boot/armbianEnv.txt, which
# the family's BOOTENV_FILE seeds. Setting it here would look like it worked and
# do nothing.
#
# https://milkv.io/duo-s
BOARD_NAME="Milk-V Duo S"
BOARD_VENDOR="milkv"
BOARDFAMILY="sophgo-sg200x-riscv64"
BOARD_MAINTAINER="lukaszsobala"
INTRODUCED="2024"
KERNEL_TARGET="edge,bleedingedge"
# Only edge is worth gating on: bleedingedge tracks a release candidate and is
# expected to break whenever it moves.
KERNEL_TEST_TARGET="edge"
BOOT_FDT_FILE="sophgo/sg2000-milkv-duo-s.dtb"

# 512MB of RAM does not go far; keep the userspace lean. PACKAGE_LIST_BOARD*
# is made readonly right after this file is sourced, so it cannot move into the
# family include even though both variants want the same thing.
PACKAGE_LIST_BOARD_REMOVE="snapd cloud-init"

enable_extension "sophgo-sg200x-aic8800"
