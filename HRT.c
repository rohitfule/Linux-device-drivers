/*
 * Rohit Fule
 * ASU ID: 1205832403
 * CSE 598: Embedded Systems Programming
 * Assignment 2 Part 1 HRT device driver
 * Description: A charachter device driver to allocate, read and reset
 * a high resolution GPTimer of the OMAP DM3730 board. 
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <plat/dmtimer.h>
#include <linux/platform_device.h> 

#define DEVICE_NAME "HRT"

/* declare the HRT device structure */
struct HRT_dev {
	char name[4];
	struct cdev cdev;
	struct omap_dm_timer *timer;
}*devp;

struct class *dev_class;
dev_t dev_num;

int device_open(struct inode *inode, struct file *file) {
    struct HRT_dev *dev = container_of(inode->i_cdev,struct HRT_dev,cdev);
    file->private_data = dev;
    printk(KERN_INFO "%s : device opened.\n",dev->name);
    return 0;
}

/* export symbol to make current timer value accessible to other kernel modules */  
unsigned int hrt_read_count(void) {
	if (devp->timer == NULL)
		return 0;
	else
		return (unsigned int)omap_dm_timer_read_counter(devp->timer); 
}
EXPORT_SYMBOL_GPL(hrt_read_count);

/* read current timer value */
ssize_t device_read(struct file *file, char* bufStoreData, size_t bufCount, loff_t* curOffset) {
	int ret = 0;
	unsigned int count;
	struct HRT_dev *dev = file->private_data;
	count = (unsigned int)omap_dm_timer_read_counter(dev->timer);
	if (bufStoreData && bufCount > 0)
		ret = copy_to_user(bufStoreData,(void *)&count,bufCount);
	if (ret)
		return -EFAULT;
	return bufCount;
}

/* control hrt device using ioctl commands */
static long device_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
	struct HRT_dev *dev = file->private_data;
	switch (cmd) {
		case 0xffc1:
			omap_dm_timer_start(dev->timer);
			break;
		case 0xffc2:
			omap_dm_timer_stop(dev->timer);
			break;
		case 0xffc3:
			omap_dm_timer_set_load(dev->timer,0,0);
			break;
		default:
			printk(KERN_ALERT "%s: got invalid case, CMD=%d\n",dev->name, cmd);
			return -EINVAL;
	}
	return 0;
}

int device_close(struct inode *inode, struct file *file) {
	struct HRT_dev *dev = file->private_data;
	printk(KERN_INFO "%s: device closed\n",dev->name);
	return 0;
}


/* file operations structure */
struct file_operations fops = {
	.owner = 			THIS_MODULE,
	.open = 			device_open,
	.release = 			device_close,
	.read = 		 	device_read,
	.unlocked_ioctl = 	device_ioctl
};

/* driver entry function */
static int __init driver_entry(void) {
	int ret;	
	
	/* allocate characher device region */
	ret = alloc_chrdev_region(&dev_num,0,1,DEVICE_NAME);
	if(ret < 0){
		printk(KERN_ALERT "HRT: failed to allocate major number\n");
		return ret;
	}
	
	/* create device class */
	dev_class = class_create(THIS_MODULE, DEVICE_NAME);
	
    /* initialize cdev struct and add */
	devp = (struct HRT_dev *)kmalloc(sizeof(struct HRT_dev),GFP_KERNEL);
	if (!devp) {
		printk(KERN_ALERT "unable to allocate memory for HRT device.\n");
		goto mem_err;
	}

	sprintf(devp->name,DEVICE_NAME);
	cdev_init(&devp->cdev,&fops);
	devp->cdev.owner = THIS_MODULE;
	
	ret = cdev_add(&devp->cdev, dev_num, 1);
    if(ret < 0) {
		printk(KERN_ALERT "%s: unable to add cdev to kernel\n",devp->name);
		goto cdev_err;
	}
	
	/* create the HRT device */
	device_create(dev_class, NULL, MKDEV(MAJOR(dev_num), 0), NULL, DEVICE_NAME);		
	
	/* allocate a dmtimer, set its value to 0 and start. */
	devp->timer = omap_dm_timer_request();
	if(!devp->timer) {
        printk(KERN_ALERT "%s: No more gp timers are available \n",devp->name);
	    goto timer_err;
    }

    /* set source clock for the timer */ 
    omap_dm_timer_set_source(devp->timer,OMAP_TIMER_SRC_SYS_CLK);
    /* set timer prescaler to 0 (1:1) */
    omap_dm_timer_set_prescaler(devp->timer,0);

	printk(KERN_INFO "%s: loaded module\n",devp->name);
	return 0;
	
	timer_err:
		device_destroy(dev_class,MKDEV(MAJOR(dev_num),0));
	cdev_err:
		kfree(devp);
	mem_err:
		unregister_chrdev_region(dev_num,1);
		class_destroy(dev_class);
	return -1;	
}


/* Driver exit function : 
 * Free timer
 * Free up allocated memory
 * Unregister charachter device region
 * Destroy charachter device and class
 */
static void driver_exit(void) {
	
	omap_dm_timer_free(devp->timer);
	printk(KERN_ALERT "HRT: timer destroyed\n");
	unregister_chrdev_region(dev_num,1);
	device_destroy(dev_class,MKDEV(MAJOR(dev_num),0));
	cdev_del(&devp->cdev);
	kfree(devp);
	class_destroy(dev_class);
	printk(KERN_INFO "HRT: unloaded module\n");
}


module_init(driver_entry);
module_exit(driver_exit);
MODULE_AUTHOR("Rohit Fule");
MODULE_DESCRIPTION("Assignment 2 Part 1 : HRT device driver");
MODULE_LICENSE("GPL v2");
