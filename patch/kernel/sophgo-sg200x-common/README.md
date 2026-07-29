# Sophgo SG200x kernel patches (shared by ARM and RISC-V)

`0001` – `0037` are taken verbatim from
[queenkjuul/milkv-duo-ubuntu][qkj] (`milkv-linux/patches/`), which collects the
pending LKML postings for the SG2000/SG2002 (CV181x) SoCs plus a few fixes of
its own. They are `git am`-format and apply cleanly to **Linux 7.0**.

`0038` – `0041` are Armbian's, and are described below.

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
the patches that touch only `arch/arm64/boot/dts/sophgo/` (`0002`, `0039` and
`0041`) are inert on a RISC-V build, since it never descends into
`arch/arm64`. Keeping
them here means one series to rebase when the kernel is bumped, and no way for
the two architectures' trees to drift apart.

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

## 0040, 0041 — Duo S USB defaults to host

`0015` enables the single dwc2 controller as `dr_mode = "otg"` but leaves the
role undecided, and nothing drives the two board lines that power the Type-A
port, so a peripheral plugged in there stays dark. These two patches - one per
architecture, otherwise identical - make host mode the boot default:

* `role-switch-default-mode = "host"` alongside `usb-role-switch`, which is
  what `dwc2_ovr_init()` reads to force the role at probe. The switch stays
  registered, so `/sys/class/usb_role/*/role` can still select device mode at
  runtime; `phy-cv1800-usb2.c` flips the ID override in the PHY's syscon
  register when it does.
* `USB_VBUS_EN` (`XGPIOB_5`) and `AUX0` (`XGPIOA_30`) as two chained
  `regulator-fixed` nodes, reached by `phy-supply` on the PHY. Each carries its
  own pinmux state, because the pin controller is `strict`, has no
  `gpio_request_enable`, and the GPIO banks declare no `gpio-ranges` - so
  requesting a line does not mux its pad.

The GPIO identities come from Milk-V's own pinmux table (in the vendor Duo S
rootfs) and match the `milkv-usb-duos` userspace switcher, which does the same
two things through `/dev/mem` and libgpiod. `AUX0` has no documented function;
it is in that script for the Duo S and not for the Duo 256M, so it is treated
as board plumbing and named accordingly.

Two known limits: VBUS follows `phy_power_on()` rather than the role, so it
stays on in device mode, and there is no real OTG detection. Modelling the
port with `gpio-usb-b-connector` would fix both - `USB_ID` is `XGPIOB_4` and
`USB_VBUS_DET` is `XGPIOB_6` - but whether either is wired on this board is
unverified, and a floating ID pin is worse than a fixed default.

## Updating

The series is version-sensitive; it is written against 7.0 and the `aic8800`
out-of-tree driver targets 7.0 as well. When bumping, re-apply with `git am`
against the new tag and refresh what fails:

```bash
git clone --depth 1 --branch vX.Y <linux> && cd linux
git am /path/to/patch/kernel/sophgo-sg200x-common/*.patch
```

[qkj]: https://github.com/queenkjuul/milkv-duo-ubuntu
