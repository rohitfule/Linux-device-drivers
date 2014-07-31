/*
 * Rohit Fule
 * CSE 598: Embedded Systems Programming
 * Assignment 2 Part 2 : Device Driver for Shared Queues 
 * Description: A charachter device driver to enqueue and dequeue
 * message tokens from two shared circular-buffers and append timestamp
 * to every message on both enqueue and dequeue operations on it. 
 */
 
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/rwsem.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/device.h>

#define DEVICE1_NAME "Squeue1"
#define DEVICE2_NAME "Squeue2"
#define MIN_STRING_LENGTH 10

/* token string lenght limits */
#define MAX_STRING_LENGTH 80	

/* maximum number of tokens per queue */
#define QUEUE_LENGTH 10				

/* symbol exported by HRT module, to read the current timer value */
extern unsigned int hrt_read_count(void);


/* token structure */
struct token
{
	unsigned int id;
	unsigned int timestamp1;
	unsigned int timestamp2;
	unsigned int timestamp3;
	unsigned int timestamp4;
	char string[MAX_STRING_LENGTH];
};

/* queue sructure */
struct Squeue {
	char name[8];
	struct cdev cdev;
	struct token *tokenp[QUEUE_LENGTH];
	int head;
	int tail;
	int count;
	int size;
	struct rw_semaphore rwsem;
}*sq1,*sq2;

struct class *dev_class;
dev_t Squeue_dev_num;

/* open appropriate queue device */
int sq_open(struct inode *inode, struct file *file) {
	struct Squeue *sq;
	sq = container_of(inode->i_cdev,struct Squeue,cdev);  
	file->private_data = sq; 
	printk(KERN_INFO "%s: Opened Device\n",sq->name);
	return 0;
}

/* Read (dequeue) tokens from appropriate device :   
 * If queue is not empty
 *     return a token structure at queue head 
 * else 
 *     return -1
 */ 
ssize_t sq_read(struct file *file, char* bufStoreData, size_t bufCount, loff_t* curOffset){
	int ret = -1;
	struct Squeue *sq = file->private_data;
	printk(KERN_INFO "%s: Reading from device\n",sq->name);
	
	if(bufStoreData && bufCount && sq->count)
	{
		/* read high resolution timer value */
		sq->tokenp[sq->head]->timestamp3 = hrt_read_count();
		ret = copy_to_user(bufStoreData,(void *)sq->tokenp[sq->head],sizeof(struct token));
		if(ret)
			return -EFAULT;		
		kfree(sq->tokenp[sq->head]);
		sq->tokenp[sq->head] = NULL;
		sq->head++;
		if(sq->head == 10) 
			sq->head = 0;
		sq->count--;
		return ret;
	}
	return ret;
}

/* Write (enqueue) tokens to appropriate device :
 * the rw semaphore ensures mutually exclusive writes in a queue  
 * If queue is not full
 *     insert a token structure at queue tail
 * else 
 *     return -1
 */ 

ssize_t sq_write(struct file *file, const char* bufSourceData, size_t bufCount, loff_t* curOffset){
	int ret = -1;
	struct Squeue *sq = file->private_data;
	if(down_write_trylock(&sq->rwsem)==0)
	{
		printk(KERN_ALERT "%s : could not lock device during open\n",sq->name);
		return ret;
	}
	if(bufSourceData && bufCount && sq->count != 10)
	{
		printk(KERN_INFO "%s: Writing to device\n",sq->name);
		sq->tokenp[sq->tail] = (struct token *)kmalloc(sizeof(struct token),GFP_KERNEL);
		ret = copy_from_user(sq->tokenp[sq->tail],(void *)bufSourceData,sizeof(struct token));
		if(ret) {
			kfree(sq->tokenp[sq->tail]);
			sq->tokenp[sq->tail] = NULL;
			return -EFAULT;
		}		
		sq->tokenp[sq->tail]->timestamp2 = hrt_read_count();
		sq->tail++;
		if(sq->tail == 10)  
			sq->tail = 0;
		sq->count++;		
	}
	up_write(&sq->rwsem);
	return ret;
}

/* close the appropriate device */
int sq_close(struct inode *inode, struct file *file) {
	struct Squeue *sq = file->private_data;
	printk(KERN_INFO "%s: Closed Device\n",sq->name);
	return 0;
}

/* file operations structure for shared queue device 1 */
struct file_operations fops_sq1 = {
	.owner = 	THIS_MODULE,
	.open = 	sq_open,
	.release = 	sq_close,
	.write = 	sq_write,
	.read = 	sq_read
};

/* file operations structure for shared queue device 2 */
struct file_operations fops_sq2 = {
	.owner = 	THIS_MODULE,
	.open = 	sq_open,
	.release = 	sq_close,
	.write = 	sq_write,
	.read = 	sq_read
};


/* driver entry function */
static int __init driver_entry(void) {
	int i=0,ret;	
	
	/* Allocate device region for two shared queues
	 * both the queue devices have same class and same major number 
	 */ 
	ret = alloc_chrdev_region(&Squeue_dev_num,0,2,"Squeue");
	if(ret < 0){
		printk(KERN_ALERT "Squeue: failed to allocate major number\n");
		return ret;
	}

	/* Create device class */   
	dev_class = class_create(THIS_MODULE, "Squeue");  
	
	sq1 = kmalloc(sizeof(struct Squeue),GFP_KERNEL);
	if(!sq1) {
		printk(KERN_ALERT "Squeue1: device kmalloc failed\n");
		return -1;
	}
	sprintf(sq1->name,DEVICE1_NAME);
	sq1->head = 0;
	sq1->tail = 0;
	sq1->size = QUEUE_LENGTH;
	sq1->count= 0;
	for(i=0;i<QUEUE_LENGTH;i++) 
		sq1->tokenp[i] = NULL;
	
	sq2 = kmalloc(sizeof(struct Squeue),GFP_KERNEL);
	if(!sq2) {
		printk(KERN_ALERT "Squeue2: device kmalloc failed\n");
		return -1;
	}
	sprintf(sq2->name,DEVICE2_NAME);
	sq2->head = 0;
	sq2->tail = 0;
	sq2->size = QUEUE_LENGTH;
	sq2->count= 0;
	for(i=0;i<QUEUE_LENGTH;i++) 
		sq2->tokenp[i] = NULL;
	
	
	/* Initialize cdev structs and add */
	cdev_init(&sq1->cdev,&fops_sq1);
	sq1->cdev.owner = THIS_MODULE;
	
	cdev_init(&sq2->cdev,&fops_sq2);
	sq2->cdev.owner = THIS_MODULE;
	
	ret = cdev_add(&sq1->cdev, MKDEV(MAJOR(Squeue_dev_num),0), 1);
	if(ret < 0) {
		kfree(sq1);
		kfree(sq2);
		printk(KERN_ALERT "Squeue1: unable to add cdev to kernel\n");
		return ret;
	}

	ret = cdev_add(&sq2->cdev, MKDEV(MAJOR(Squeue_dev_num),1), 1);
   	if(ret < 0) {
		cdev_del(&sq1->cdev);
		kfree(sq1);
		kfree(sq2);		
		printk(KERN_ALERT "Squeue2: unable to add cdev to kernel\n");
		return ret;
	}
	
	/* initialize rw semaphore */
	init_rwsem(&sq1->rwsem);
	init_rwsem(&sq2->rwsem);
	
	/* create devices */    
	device_create(dev_class, NULL, MKDEV(MAJOR(Squeue_dev_num), 0), NULL, DEVICE1_NAME);		
	device_create(dev_class, NULL, MKDEV(MAJOR(Squeue_dev_num), 1), NULL, DEVICE2_NAME);		
	
    printk(KERN_INFO "Squeue: loaded module\n");
	return 0;
}


/* Driver exit function : 
 * Free up allocated memory
 * Unregister charachter device region
 * Destroy charachter devices and class
 */
static void driver_exit(void) {
	
	unregister_chrdev_region(Squeue_dev_num,2);
	device_destroy(dev_class,MKDEV(MAJOR(Squeue_dev_num),0));
	device_destroy(dev_class,MKDEV(MAJOR(Squeue_dev_num),1));
	cdev_del(&sq1->cdev);
	cdev_del(&sq2->cdev);
	kfree(sq1);
	kfree(sq2);
	class_destroy(dev_class);
	printk(KERN_ALERT "Squeue: unloaded module\n");
}


module_init(driver_entry);
module_exit(driver_exit);
MODULE_AUTHOR("Rohit Fule");
MODULE_DESCRIPTION("Assignment 2 Part 2 : Shared Queue Device Driver");
MODULE_LICENSE("GPL v2");

