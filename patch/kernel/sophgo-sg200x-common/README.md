# Sophgo SG200x kernel patches (shared by ARM and RISC-V)

`0001` – `0037` are taken verbatim from
[queenkjuul/milkv-duo-ubuntu][qkj] (`milkv-linux/patches/`), which collects the
pending LKML postings for the SG2000/SG2002 (CV181x) SoCs plus a few fixes of
its own. They are `git am`-format and apply cleanly to **Linux 7.0**.

`0038` is Armbian's, and is described below.

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
`KERNELPATCHDIR` at this directory. arm64 adds
`patch/kernel/sophgo-sg200x-arm64` on top for its own board DTS.

## 0038 — SOC_PERIPHERAL_IRQ for dual-arch nodes

Three nodes added by the upstream series hardcode raw PLIC interrupt numbers
into that shared `cv180x.dtsi`, which is wrong on arm64: the mailbox also
carried `interrupt-parent = <&plic>`, a label arm64 does not have, and the
watchdog and thermal sensor would have landed on GIC SPIs 58 and 16 instead of
42 and 0. `0038` converts all three to `SOC_PERIPHERAL_IRQ()` and moves the
mailbox inside the `soc` node so it inherits the right interrupt parent.

Drop this patch if the numbering is fixed upstream.

## Updating

The series is version-sensitive; it is written against 7.0 and the `aic8800`
out-of-tree driver targets 7.0 as well. When bumping, re-apply with `git am`
against the new tag and refresh what fails:

```bash
git clone --depth 1 --branch vX.Y <linux> && cd linux
git am /path/to/patch/kernel/sophgo-sg200x-common/*.patch
```

[qkj]: https://github.com/queenkjuul/milkv-duo-ubuntu
