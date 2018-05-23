#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/errno.h>

#include "ram_device.h"
#include "partition.h"

#include <linux/io.h>

#define RB_DEVICE_SIZE 1024 /* sectors */
/* So, total device size = 1024 * 512 bytes = 512 KiB */

/* Array where the disk stores its data */
static void __iomem *dev_data;

int ramdevice_init(void)
{
	dev_data = vmalloc(RB_DEVICE_SIZE * RB_SECTOR_SIZE);
	if (dev_data == NULL)
		return -ENOMEM;
	/* Setup its partition table */
	copy_mbr_n_br(dev_data);
	return RB_DEVICE_SIZE;
}

void ramdevice_cleanup(void)
{
	vfree(dev_data);
}

void ramdevice_write(sector_t sector_off, u8 *buffer, unsigned int sectors)
{
	memcpy(dev_data + sector_off * RB_SECTOR_SIZE, buffer,
		sectors * RB_SECTOR_SIZE);
}
void ramdevice_read(sector_t sector_off, u8 *buffer, unsigned int sectors)
{
	memcpy(buffer, dev_data + sector_off * RB_SECTOR_SIZE,
		sectors * RB_SECTOR_SIZE);
}

int pci_ramdevice_init(void __iomem * bar){
	pci_copy_mbr_n_br(bar);
	printk("CDNS ram devicedone \n");
	dev_data = bar;
	return RB_DEVICE_SIZE;
}

void pci_ramdevice_cleanup(void)
{	
	printk("CDNS nothing to do it for cleaning\n");
}

void pci_ramdevice_write(sector_t sector_off, u8 *buffer, unsigned int sectors)
{
	int index, i;
	void __iomem *addr = dev_data + sector_off * RB_SECTOR_SIZE;
	unsigned int lsectors = sectors * RB_SECTOR_SIZE;
	
	for (index = 0, i = 0; index < lsectors; index += 4, i++)
		iowrite32(*((u32 *)buffer + i), addr + index);
}
void pci_ramdevice_read(sector_t sector_off, u8 *buffer, unsigned int sectors)
{
	int index, i;
	void __iomem *addr = dev_data + sector_off * RB_SECTOR_SIZE;
	for (index = 0, i = 0; index < sectors * RB_SECTOR_SIZE; index += 4, i++)
		*((u32 *)buffer + i) = ioread32(addr + index);
}
