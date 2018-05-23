#ifndef PARTITION_H
#define PARTITION_H

#include <linux/types.h>

extern void copy_mbr_n_br(u8 *disk);
extern void pci_copy_mbr_n_br(void __iomem *disk);
#endif
