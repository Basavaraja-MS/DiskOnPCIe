/* Disk on RAM Driver */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/genhd.h> // For basic block driver framework
#include <linux/blkdev.h> // For at least, struct block_device_operations
#include <linux/hdreg.h> // For struct hd_geometry
#include <linux/errno.h>

#include "ram_device.h"

#include <linux/pci.h>

#define RB_FIRST_MINOR 0
#define RB_MINOR_CNT 16

static u_int rb_major = 0;

#define PCI_VENDOR_ID_CDNS      0x17CD
#define PCI_DEVICE_ID_CDNS      0x0100
static struct gendisk *local_rb_disk;

static int cdns_pci_probe(struct pci_dev *dev, const struct pci_device_id *id);
static void cdns_pci_remove(struct pci_dev *dev);


static const struct pci_device_id cdns_ids[ ] = {
        { PCI_DEVICE(PCI_VENDOR_ID_CDNS, PCI_DEVICE_ID_CDNS) },
};

static struct pci_driver pci_driver = {
        .name = "pci_cdns",
        .id_table = cdns_ids,
        .probe = cdns_pci_probe,
        .remove = cdns_pci_remove,
};


enum bars {bar0, bar1, bar2, bar3, bar4, bar5};
void __iomem *bar0base;
static int cdns_pci_probe(struct pci_dev *dev, const struct pci_device_id *id){
        int retval, bars;
        u16 u16data;

        printk("CDNS pobed\n");
        bars = pci_select_bars(dev, IORESOURCE_MEM);
        printk ("CDNS bars retval 0x%x\n", bars);

        if (pci_request_region(dev, bar0,"cdn_pci_driver")==0)
                printk("CDNS region requested properly\n");
        else{
                printk("CDNS Error coudnt allocate regions\n");
                return -1;
        }

        bar0base = pci_iomap(dev, bar0, 0);
        pci_set_master(dev);

        retval = pci_enable_device(dev);
        if (retval < 0){
                printk("CDNS Enable pci device failed\n");
                goto devicedel;
        }

        u16data = readw(bar0base);
        printk("CDNS u16data 0x0 - 0x%x\n", u16data);
	if ((retval = pci_ramdevice_init(bar0base)) < 0)
	{
		printk("CDNS Error in ramdevice init");
		return retval;
	}
	/* Adding the disk to the system */
	add_disk(local_rb_disk);


        return 0;

devicedel:
	pci_disable_device(dev);
	pci_iounmap(dev, bar0base);
	pci_release_regions(dev);
        return -1;
}

static void cdns_pci_remove(struct pci_dev *dev){
	pci_disable_device(dev);
        pci_iounmap(dev, bar0base);
        pci_release_regions(dev);
}



/* 
 * The internal structure representation of our Device
 */
static struct rb_device
{
	/* Size is the size of the device (in sectors) */
	unsigned int size;
	/* For exclusive access to our request queue */
	spinlock_t lock;
	/* Our request queue */
	struct request_queue *rb_queue;
	/* This is kernel's representation of an individual disk device */
	struct gendisk *rb_disk;
	/*PCIe devoice */
	struct pci_dev *pdev;
} rb_dev;

static int rb_open(struct block_device *bdev, fmode_t mode)
{
	unsigned unit = iminor(bdev->bd_inode);

	printk(KERN_INFO "rb: Device is opened\n");
	printk(KERN_INFO "rb: Inode number is %d\n", unit);

	if (unit > RB_MINOR_CNT)
		return -ENODEV;
	return 0;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0))
static int rb_close(struct gendisk *disk, fmode_t mode)
{
	printk(KERN_INFO "rb: Device is closed\n");
	return 0;
}
#else
static void rb_close(struct gendisk *disk, fmode_t mode)
{
	printk(KERN_INFO "rb: Device is closed\n");
}
#endif

static int rb_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	geo->heads = 1;
	geo->cylinders = 32;
	geo->sectors = 32;
	geo->start = 0;
	return 0;
}

/* 
 * Actual Data transfer
 */
static int rb_transfer(struct request *req)
{
	//struct rb_device *dev = (struct rb_device *)(req->rq_disk->private_data);

	int dir = rq_data_dir(req);
	sector_t start_sector = blk_rq_pos(req);
	unsigned int sector_cnt = blk_rq_sectors(req);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0))
#define BV_PAGE(bv) ((bv)->bv_page)
#define BV_OFFSET(bv) ((bv)->bv_offset)
#define BV_LEN(bv) ((bv)->bv_len)
	struct bio_vec *bv;
#else
#define BV_PAGE(bv) ((bv).bv_page)
#define BV_OFFSET(bv) ((bv).bv_offset)
#define BV_LEN(bv) ((bv).bv_len)
	struct bio_vec bv;
#endif
	struct req_iterator iter;

	sector_t sector_offset;
	unsigned int sectors;
	u8 *buffer;

	int ret = 0;

	//printk(KERN_DEBUG "rb: Dir:%d; Sec:%lld; Cnt:%d\n", dir, start_sector, sector_cnt);

	sector_offset = 0;
	rq_for_each_segment(bv, req, iter)
	{
		buffer = page_address(BV_PAGE(bv)) + BV_OFFSET(bv);
		if (BV_LEN(bv) % RB_SECTOR_SIZE != 0)
		{
			printk(KERN_ERR "rb: Should never happen: "
				"bio size (%d) is not a multiple of RB_SECTOR_SIZE (%d).\n"
				"This may lead to data truncation.\n",
				BV_LEN(bv), RB_SECTOR_SIZE);
			ret = -EIO;
		}
		sectors = BV_LEN(bv) / RB_SECTOR_SIZE;
		printk(KERN_DEBUG "rb: Start Sector: %llu, Sector Offset: %llu; Buffer: %p; Length: %u sectors\n",
			(unsigned long long)(start_sector), (unsigned long long)(sector_offset), buffer, sectors);
		if (dir == WRITE) /* Write to the device */
		{
			pci_ramdevice_write(start_sector + sector_offset, buffer, sectors);
		}
		else /* Read from the device */
		{
			pci_ramdevice_read(start_sector + sector_offset, buffer, sectors);
		}
		sector_offset += sectors;
	}
	if (sector_offset != sector_cnt)
	{
		printk(KERN_ERR "rb: bio info doesn't match with the request info");
		ret = -EIO;
	}

	return ret;
}
	
/*
 * Represents a block I/O request for us to execute
 */
static void rb_request(struct request_queue *q)
{
	struct request *req;
	int ret;

	/* Gets the current request from the dispatch queue */
	while ((req = blk_fetch_request(q)) != NULL)
	{
#if 0
		/*
		 * This function tells us whether we are looking at a filesystem request
		 * - one that moves block of data
		 */
		if (!blk_fs_request(req))
		{
			printk(KERN_NOTICE "rb: Skip non-fs request\n");
			/* We pass 0 to indicate that we successfully completed the request */
			__blk_end_request_all(req, 0);
			//__blk_end_request(req, 0, blk_rq_bytes(req));
			continue;
		}
#endif
		ret = rb_transfer(req);
		__blk_end_request_all(req, ret);
		//__blk_end_request(req, ret, blk_rq_bytes(req));
	}
}

/* 
 * These are the file operations that performed on the ram block device
 */
static struct block_device_operations rb_fops =
{
	.owner = THIS_MODULE,
	.open = rb_open,
	.release = rb_close,
	.getgeo = rb_getgeo,
};
	
/* 
 * This is the registration and initialization section of the ram block device
 * driver
 */
static int __init rb_init(void)
{
	int ret;
#if 0
	/* Set up our RAM Device */
	if ((ret = ramdevice_init()) < 0)
	{
		return ret;
	}
	rb_dev.size = ret; 
#endif
	rb_dev.size = 1024; 

	/* Get Registered */
	rb_major = register_blkdev(rb_major, "rb");
	if (rb_major <= 0)
	{
		printk(KERN_ERR "rb: Unable to get Major Number\n");
		ramdevice_cleanup();
		return -EBUSY;
	}

	/* Get a request queue (here queue is created) */
	spin_lock_init(&rb_dev.lock);
	rb_dev.rb_queue = blk_init_queue(rb_request, &rb_dev.lock);
	if (rb_dev.rb_queue == NULL)
	{
		printk(KERN_ERR "rb: blk_init_queue failure\n");
		unregister_blkdev(rb_major, "rb");
		ramdevice_cleanup();
		return -ENOMEM;
	}
	
	/*
	 * Add the gendisk structure
	 * By using this memory allocation is involved, 
	 * the minor number we need to pass bcz the device 
	 * will support this much partitions 
	 */
	rb_dev.rb_disk = alloc_disk(RB_MINOR_CNT);
	if (!rb_dev.rb_disk)
	{
		printk(KERN_ERR "rb: alloc_disk failure\n");
		blk_cleanup_queue(rb_dev.rb_queue);
		unregister_blkdev(rb_major, "rb");
		ramdevice_cleanup();
		return -ENOMEM;
	}

 	/* Setting the major number */
	rb_dev.rb_disk->major = rb_major;
  	/* Setting the first mior number */
	rb_dev.rb_disk->first_minor = RB_FIRST_MINOR;
 	/* Initializing the device operations */
	rb_dev.rb_disk->fops = &rb_fops;
 	/* Driver-specific own internal data */
	rb_dev.rb_disk->private_data = &rb_dev;
	rb_dev.rb_disk->queue = rb_dev.rb_queue;

	local_rb_disk = rb_dev.rb_disk;

	/*
	 * You do not want partition information to show up in 
	 * cat /proc/partitions set this flags
	 */
	//rb_dev.rb_disk->flags = GENHD_FL_SUPPRESS_PARTITION_INFO;
	sprintf(rb_dev.rb_disk->disk_name, "rb");
	/* Setting the capacity of the device in its gendisk structure */
	set_capacity(rb_dev.rb_disk, rb_dev.size);

        if (pci_register_driver(&pci_driver) < 0){
                printk("CDNS: Error in pci_rigister\n");
		blk_cleanup_queue(rb_dev.rb_queue);
		unregister_blkdev(rb_major, "rb");
		ramdevice_cleanup();
		return -ENOMEM;

        }


#if 0
	/* Adding the disk to the system */
	add_disk(rb_dev.rb_disk);
	/* Now the disk is "live" */
	printk(KERN_INFO "rb: Ram Block driver initialised (%d sectors; %d bytes)\n",
		rb_dev.size, rb_dev.size * RB_SECTOR_SIZE);
#endif
	return 0;
}
/*
 * This is the unregistration and uninitialization section of the ram block
 * device driver
 */
static void __exit rb_cleanup(void)
{
	pci_unregister_driver(&pci_driver);
	del_gendisk(rb_dev.rb_disk);
	put_disk(rb_dev.rb_disk);
	blk_cleanup_queue(rb_dev.rb_queue);
	unregister_blkdev(rb_major, "rb");
	ramdevice_cleanup();
}

module_init(rb_init);
module_exit(rb_cleanup);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Ram Block Driver");
MODULE_ALIAS_BLOCKDEV_MAJOR(rb_major);
