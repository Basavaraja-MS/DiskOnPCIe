#ifndef RAMDEVICE_H
#define RAMDEVICE_H

#define RB_SECTOR_SIZE 512

extern int ramdevice_init(void);
extern void ramdevice_cleanup(void);
extern void ramdevice_write(sector_t sector_off, u8 *buffer, unsigned int sectors);
extern void ramdevice_read(sector_t sector_off, u8 *buffer, unsigned int sectors);

extern int pci_ramdevice_init(void __iomem *bar);
extern void pci_ramdevice_cleanup(void);
extern void pci_ramdevice_write(sector_t sector_off, u8 *buffer, unsigned int sectors);
extern void pci_ramdevice_read(sector_t sector_off, u8 *buffer, unsigned int sectors);
#endif
