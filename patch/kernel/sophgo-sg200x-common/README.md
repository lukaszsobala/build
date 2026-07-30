# Sophgo SG200x kernel patches (shared by ARM and RISC-V)

`0001` – `0037` are taken verbatim from
[queenkjuul/milkv-duo-ubuntu][qkj] (`milkv-linux/patches/`), which collects the
pending LKML postings for the SG2000/SG2002 (CV181x) SoCs plus a few fixes of
its own. They are `git am`-format and apply cleanly to **Linux 7.0**.

`0038` – `0047` are Armbian's, and are described below.

## Why one series for two architectures

`arch/arm64/boot/dts/sophgo/sg2000.dtsi` does this:

```c
#define SOC_PERIPHERAL_IRQ(nr)		GIC_SPI (nr)
#include <riscv/sophgo/cv180x.dtsi>
#include <riscv/sophgo/cv181x.dtsi>
```

The entire SoC description lives under `arch/riscv`, and the arm64 side reuses
it with a different interrupt-specifier macro. So the DTS patches that look
RISC-V-only are in fact needed by both builds, and both families point
`KERNELPATCHDIR` at this directory.

The reverse also holds, which is why there is no second directory for arm64:
the patches that touch only `arch/arm64/boot/dts/sophgo/` (`0002`, `0039`,
`0041`, `0043`, `0045` and `0047`) are inert on a RISC-V build, since it never
descends into `arch/arm64`. Keeping them here means one series to rebase
when the kernel is bumped, and no way for the two architectures' trees to drift
apart.

The `edge` branch adds `sophgo-sg200x-dmac-7.0` on top, on both architectures;
that backport is version-specific rather than arch-specific.

## 0038 — SOC_PERIPHERAL_IRQ for dual-arch nodes

Three nodes added by the upstream series hardcode raw PLIC interrupt numbers
into that shared `cv180x.dtsi`, which is wrong on arm64: the mailbox also
carried `interrupt-parent = <&plic>`, a label arm64 does not have, and the
watchdog and thermal sensor would have landed on GIC SPIs 58 and 16 instead of
42 and 0. `0038` converts all three to `SOC_PERIPHERAL_IRQ()` and moves the
mailbox inside the `soc` node so it inherits the right interrupt parent.

Drop this patch if the numbering is fixed upstream.

## 0039 — arm64 board DTS parity

The initial arm64 Milk-V Duo S device tree (`0002`) only brings up the console,
SD card and Ethernet. Since the SoC description is shared, every peripheral
enabled for the RISC-V Duo S is equally available on the Cortex-A53, so `0039`
brings `arch/arm64/boot/dts/sophgo/sg2000-milkv-duo-s.dts` to parity with its
RISC-V counterpart: I2C1-4, SPI0-3, UART1-4, PWM0-3, watchdog, eFuse, USB OTG,
the I2S/internal DAC+ADC sound card and the AIC8800D80 SDIO Wi-Fi slot. It also
adds the C906L coprocessor reserved-memory region and remoteproc node to
`sg2000.dtsi`, disabled there and enabled per-board.

It applies on top of `0002` and touches nothing outside `arch/arm64`, so a
RISC-V build carries it without noticing.

## 0040, 0041 — Duo S USB port in host mode

`0015` enables the single dwc2 controller as `dr_mode = "otg"` but leaves the
role undecided, and nothing drives the two board lines that power the Type-A
port, so a peripheral plugged in there stays dark. These two patches - one per
architecture, otherwise identical - fix both halves:

* `dr_mode = "host"`. Not `otg` with a role switch: **`usb-role-switch` hangs
  the boot on this SoC.** See below.
* `USB_VBUS_EN` (`XGPIOB_5`) and `AUX0` (`XGPIOA_30`) as two chained
  `regulator-fixed` nodes, reached by `phy-supply` on the PHY. Each carries its
  own pinmux state, because the pin controller is `strict`, has no
  `gpio_request_enable`, and the GPIO banks declare no `gpio-ranges` - so
  requesting a line does not mux its pad. Both groups declare
  `power-source = <1800>`, which is mandatory (see the pinmux note below).

The GPIO identities come from Milk-V's own pinmux table (in the vendor Duo S
rootfs) and match the `milkv-usb-duos` userspace switcher, which does the same
two things through `/dev/mem` and libgpiod. `AUX0` has no documented function;
it is in that script for the Duo S and not for the Duo 256M, so it is treated
as board plumbing and named accordingly.

### Do not add usb-role-switch

The tidier-looking arrangement - keep `dr_mode = "otg"`, add `usb-role-switch`
and `role-switch-default-mode = "host"`, and keep the ability to select device
mode at runtime through `/sys/class/usb_role/*/role` - **wedges the boot.**

`dwc2_drd_init()` runs `dwc2_ovr_init()`, which forces the session-valid
overrides in `GOTGCTL` and calls `dwc2_force_mode()`. `dwc2_hcd_init()` still
finishes, and the `usb_add_gadget_udc()` after it never returns. Because
`4340000.usb` probes deferred, `deferred_probe_initcall()` never returns
either - and `clk_disable_unused()` is `late_initcall_sync`, i.e. strictly
after it. So the machine stops with no panic and no further output at all:

```text
hub 1-0:1.0: 1 port detected
probe of 1-0:1.0 returned 0 after 8613 usecs
probe of usb1 returned 0 after 14736 usecs
<nothing, ever>
```

That signature is worth recognising, because it looks like a hardware hang and
is not one. `initcall_debug` on the kernel command line is what makes it
legible: `probe of 4340000.usb returned 0` never appears, and the last
`calling` line is `deferred_probe_initcall`. Any driver that wedges a deferred
probe kills the boot the same silent way.

`dr_mode = "host"` sidesteps all of it - dwc2 leaves `gadget_enabled` clear, so
neither the gadget nor the role switch is built. The price is the Type-C
receptacle's device mode, permanently. This board can pay it: it has ethernet,
so headless setup does not need USB networking.

The other known limit is that there is no real OTG detection. Modelling the
port with `gpio-usb-b-connector` would give it - `USB_ID` is `XGPIOB_4` and
`USB_VBUS_DET` is `XGPIOB_6` - but whether either is wired on this board is
unverified, and a floating ID pin is worse than a fixed default.

## 0042, 0043 — Duo S eMMC pads muxed from the DT

On a Duo S with eMMC fitted, booting from a card left the eMMC invisible: the
controller registered and then `mmc2: Failed to initialize a non-removable
card`. Not a bus conflict with the card slot - they are separate controllers,
and the installer drives both in one U-Boot session - the seven eMMC pads were
simply never routed.

`board/cvitek/cv181x/board.c` in the vendor bootloader routes them only under
`CONFIG_EMMC_SUPPORT`, which comes from `STORAGE_TYPE=emmc` at bootloader build
time, because the same pads carry SPI-NAND and SPI-NOR on boards that have such
flash. So the eMMC bootloader routes them and the SD bootloader does not, and
Linux inherited whatever the BootROM left. These patches declare the group on
`&emmc` so it no longer depends on which bootloader ran.

Mux plus `power-source = <1800>`, nothing else: `PINMUX_CONFIG()` in the
bootloader sets the function select alone, and the eMMC runs at those defaults
when it is the boot device - but `power-source` is not optional, see the pinmux
note below.

Verified on a Duo S with eMMC, booted from a card: `mmcblk2` appears with its
partitions, plus the `boot0`/`boot1`/`rpmb` areas.

Note this DTS is shared with the Duo S that has no eMMC fitted, where it pins
seven pads for an absent chip. Inert unless those pads are wired elsewhere on
that variant, which nothing in the vendor sources suggests.

### Why no-1-8-v stays

Tempting to remove, and it does not work. `no-1-8-v` caps the eMMC at high
speed - `new high speed MMC card` in every boot log - which on a 4-bit bus is
26 MB/s, and the hardware looks capable of more: the vendor bootloader tunes
this controller (`mmc0 : finished tuning`, an HS200 step) and writes at
42.8 MiB/s during an install. The driver side is there too,
`dwcmshc_set_uhs_signaling()` and `cv18xx_sdhci_execute_tuning()` are both
wired up for `sophgo,cv1800b-dwcmshc`.

Dropping `no-1-8-v` for `mmc-ddr-1_8v` + `mmc-hs200-1_8v` was tried anyway, and
**the eMMC then does not enumerate at all**: the controller registers and no
card line ever follows. That is what a failed voltage switch looks like -
`mmc_select_hs200()` returns something other than `-EBADMSG`,
`mmc_select_timing()` propagates it, and card init fails outright instead of
falling back. Note how much worse this is than the bug it was meant to
improve on, and that it is silent.

So high speed stands until somebody establishes what `VDDIO_EMMC` actually is
on this board. `mmc-ddr-1_8v` on its own is untried and would be the next thing
to measure; it needs no tuning. `max-frequency` is kept because it was in the
verified tree, and is inert while `no-1-8-v` holds.

## 0044, 0045 — disable the unused coprocessor

The SG2000 has a second, much smaller RISC-V core (C906L) for real-time work with
no OS in the way; Milk-V's "Arduino on the Duo" runs sketches there on FreeRTOS,
and remoteproc is how Linux would load firmware onto it. We build no FreeRTOS
image for it (see `sophgo-sg200x_uboot.inc`) and install no `cvirtos.elf`, so it
never starts - while its reserved-memory region held 2MiB at the top of a 512MiB
board.

Both go: reserved-memory regions are honoured regardless of whether their consumer
is enabled, so disabling the node alone would have freed nothing. The region is
deleted **by label**, because its node name differs per architecture -
`region@8fe00000` on riscv64 (upstream named it inconsistently with its own `reg`)
and `region@9fe00000` on arm64 - and `/delete-node/` on a name that does not exist
is silently accepted.

## 0046, 0047 — uart2 pads are Bluetooth

Comment only. `uart2` is enabled but its pads are routed to `uart4` by
`cvi_board_init()`, and `uart4` is the Bluetooth controller, so routing uart2
takes Bluetooth down. See the pinmux note below.

## Which peripherals actually have pins

The board DTS enables far more than the board routes. `cvi_board_init()` in the
vendor bootloader runs in both storage builds and routes: I2C2, I2C3, I2C4, SPI3,
UART4 (Bluetooth), the camera pins, Ethernet LEDs, `USB_VBUS_EN`, and some GPIOs.
`PINMUX_SDIO1` (Wi-Fi) is unconditional; the eMMC group is storage-conditional,
which is what `0042`/`0043` fix; the card slot's pads happen to sit right after
either boot.

**Every pinctrl group must carry `power-source`.** `cv1800_dt_node_to_map_post()`
reads it with `of_property_read_u32()` and returns the error if it is absent,
which fails the whole pin map - so the *consumer* never probes. A group without
it does not warn, it makes the device disappear: `4300000.mmc` vanished from
dmesg entirely, and the USB regulators sat in `deferred probe pending` forever,
taking the PHY and the controller with them. Every in-tree Sophgo board sets it.
Use `1800` or `3300`, and keep it consistent per power domain -
`cv1800_set_power_cfg()` rejects a second, different value for a domain it has
already seen.

It is bookkeeping, not a rail switch: the stored value feeds the drive-strength
and schmitt translation in pinconf, and writes no voltage register of its own.
Domains span more than their name suggests - `VDDIO_EMMC` in `pinctrl-sg2000.c`
covers `UART0_RX`/`UART0_TX` (the console), `SD0_CD`, `SD0_PWR_EN`, `IIC0` and
`AUX0` as well as the seven eMMC pads - so check `pinctrl-sg2000.c` before
assuming a group is electrically on its own.

Everything else (`i2c1`, `spi0`-`spi2`, `uart1`, `uart2`, `uart3`, and
`pwm0`-`pwm3`) is enabled with no pins routed. The device nodes appear and do
nothing until something routes their pads, which is deliberate: those pads are
on the pin header, so any fixed choice here would be wrong for somebody. Route
them with a device tree overlay, or Milk-V's `duo-pinmux`.

## Updating

The series is version-sensitive; it is written against 7.0 and the `aic8800`
out-of-tree driver targets 7.0 as well. When bumping, re-apply with `git am`
against the new tag and refresh what fails:

```bash
git clone --depth 1 --branch vX.Y <linux> && cd linux
git am /path/to/patch/kernel/sophgo-sg200x-common/*.patch
```

[qkj]: https://github.com/queenkjuul/milkv-duo-ubuntu
