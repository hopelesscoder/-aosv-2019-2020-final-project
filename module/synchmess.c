#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h> 
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/version.h>
#include <linux/uaccess.h>
#include <asm-generic/errno-base.h>
#include <asm/uaccess.h>

#include "synchmess-ioctl.h"

MODULE_AUTHOR("Daniele Pasquini <pasqdaniele@gmail.com>");
MODULE_DESCRIPTION("Thread Synchronization and Messaging Subsystem");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");


long synchmess_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
int synchmess_open(struct inode *inode, struct file *filp);
int synchmess_release(struct inode *inode, struct file *filp);

// File operations for the module synchmess
struct file_operations synchmess_fops = {
	open: synchmess_open,
	unlocked_ioctl: synchmess_ioctl,
	compat_ioctl: synchmess_ioctl,
	release: synchmess_release
};

long synchmess_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	group_t group;

	switch (cmd) {
        case IOCTL_INSTALL_GROUP:
			printk("Called ioctl()\n");
			copy_from_user(&group, (group_t *)arg, sizeof(group_t));
			printk("%s\n", group.name);
			goto out;
	}

    out:
	return ret;
}

int synchmess_open(struct inode *inode, struct file *filp) {
    printk(KERN_INFO "%s: Open operation.\n", KBUILD_MODNAME);
	return 0;
}


int synchmess_release(struct inode *inode, struct file *filp){
    printk(KERN_INFO "%s: Release operation.\n", KBUILD_MODNAME);
	return 0;
}

// Variables to correctly setup/shutdown the pseudo device file
static int synchmess_major;
static struct class *synchmess_dev_cl = NULL;
static struct device *synchmess_device = NULL;

static int __init synchmess_init(void){
    int err;
    
    printk(KERN_INFO "%s: Init module.\n", KBUILD_MODNAME);
    
	synchmess_major = register_chrdev(0, KBUILD_MODNAME, &synchmess_fops);

	// Dynamically allocate a major for the synchmess device
	if (synchmess_major < 0) {
		printk(KERN_ERR "%s: Failed registering char device\n", KBUILD_MODNAME);
		err = synchmess_major;
		goto failed_chrdevreg;
	}

	// Create a class for the synchmess device
	synchmess_dev_cl = class_create(THIS_MODULE, "synchmess");
	if (IS_ERR(synchmess_dev_cl)) {
		printk(KERN_ERR "%s: failed to register device class\n", KBUILD_MODNAME);
		err = PTR_ERR(synchmess_dev_cl);
		goto failed_classreg;
	}

	// Create a device in the previously created class
	synchmess_device = device_create(synchmess_dev_cl, NULL, MKDEV(synchmess_major, 0), NULL, KBUILD_MODNAME);
	if (IS_ERR(synchmess_device)) {
		printk(KERN_ERR "%s: failed to create device\n", KBUILD_MODNAME);
		err = PTR_ERR(synchmess_device);
		goto failed_devreg;
	}

	printk(KERN_INFO "%s: special device registered with major number %d\n", KBUILD_MODNAME, synchmess_major);

	return 0;

failed_devreg:
	class_unregister(synchmess_dev_cl);
	class_destroy(synchmess_dev_cl);
failed_classreg:
	unregister_chrdev(synchmess_major, KBUILD_MODNAME);
failed_chrdevreg:
	return err;// Non-zero return means that the module couldn't be loaded.
}

static void __exit synchmess_cleanup(void)
{
    printk(KERN_INFO "%s: Cleaning up module.\n", KBUILD_MODNAME);
    device_destroy(synchmess_dev_cl, MKDEV(synchmess_major, 0));
	class_unregister(synchmess_dev_cl);
	class_destroy(synchmess_dev_cl);
	unregister_chrdev(synchmess_major, KBUILD_MODNAME);
}

module_init(synchmess_init);
module_exit(synchmess_cleanup);
