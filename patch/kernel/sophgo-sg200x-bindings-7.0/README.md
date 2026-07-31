# SG2000 device tree bindings, for kernels older than 7.2

`0001` documents the `milkv,duo-s` board compatibles and `0003` the
`sophgo,sg2000-plic` / `sophgo,sg2000-clint` strings. Both went upstream in
7.2-rc1, through the `soc-dt-7.2` pull, as the very same commits:

```
efe66eed43ef dt-bindings: soc: sophgo: add Milk-V Duo S board compatibles
972e8823d938 dt-bindings: soc: sophgo: add sg2000 plic and clint documentation
```

So they are applied *only* on the 7.0 (`edge`) branch. On 7.2 (`bleedingedge`)
`patch` reports "Reversed (or previously applied) patch detected" and exits 1,
and `process_patch_file` turns any non-zero exit into a failed build.

Nothing but `Documentation/devicetree/bindings/` is touched, so this costs a
7.0 build nothing at compile time; what it buys is `./compile.sh dts-check`
still having a schema to check the Duo S device tree against.

They keep their original 0001/0003 numbering, which is also why the gap in
`sophgo-sg200x-common` starts at 0002. Directories listed in `KERNELPATCHDIR`
are applied one after another rather than merged and re-sorted, so these two
land after the main series - which does not matter here, as no other patch in
the series touches these three files.

Delete this directory once `edge` moves past 7.0.
