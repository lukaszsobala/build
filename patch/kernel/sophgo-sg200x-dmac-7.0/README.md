# CV1800B DMA support, for kernels older than 7.1

`snps,dw-axi-dmac` gained the `sophgo,cv1800b-axi-dma` compatible and its
driver support in Linux 7.1. These two patches backport it and are applied
*only* on the 7.0 (`edge`) branch — applying them on 7.1 (`bleedingedge`) fails,
because the code is already there.

They keep their original 0024/0025 numbering so that they still sort into the
right place in the combined series; Armbian merges the patch directories listed
in `KERNELPATCHDIR` and applies everything in filename order.

Delete this directory once `edge` moves past 7.0.
