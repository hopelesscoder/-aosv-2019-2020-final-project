#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h> 
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/version.h>
#include <linux/uaccess.h>
#include <asm-generic/errno-base.h>
#include <asm/uaccess.h>
#include <asm/segment.h>
#include <linux/buffer_head.h>
#include <linux/list.h>
#include <linux/slab.h>

#include "synchmess-ioctl.h"

MODULE_AUTHOR("Daniele Pasquini <pasqdaniele@gmail.com>");
MODULE_DESCRIPTION("Thread Synchronization and Messaging Subsystem");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");


struct message_t {
    char text[32];
    struct list_head list;
};

struct group_dev {
	dev_t devt;
    char group_dev_name[32];
    struct message_t message;
    struct list_head list;
};


struct list_head group_list;

long synchmess_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
int synchmess_open(struct inode *inode, struct file *filp);
int synchmess_release(struct inode *inode, struct file *filp);

long synchgroup_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
int synchgroup_open(struct inode *inode, struct file *filp);
int synchgroup_release(struct inode *inode, struct file *filp);
ssize_t synchgroup_read (struct file *file, char __user *buf, size_t count, loff_t *offset);
ssize_t synchgroup_write (struct file *file, const char __user *buf, size_t count, loff_t *offset);

// File operations for the device synchmess
struct file_operations synchmess_fops = {
	open: synchmess_open,
	unlocked_ioctl: synchmess_ioctl,
	compat_ioctl: synchmess_ioctl,
	release: synchmess_release
};

// File operations for the device synchgroup
struct file_operations synchgroup_fops = {
	open: synchgroup_open,
	unlocked_ioctl: synchgroup_ioctl,
	compat_ioctl: synchgroup_ioctl,
	release: synchgroup_release,
    read: synchgroup_read,
    write: synchgroup_write
};

static int synchgroup_major;
static struct class *synchgroup_dev_cl = NULL;
static struct device *synchgroup_device = NULL;

//for sleep/awake
long synchgroup_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long ret = 0;

	switch (cmd) {
			goto out;
	}

    out:
	return ret;
}

int synchgroup_open(struct inode *inode, struct file *filp) {
    printk(KERN_INFO "%s: Open operation, synchgroup device.\n", KBUILD_MODNAME);
	return 0;
}


int synchgroup_release(struct inode *inode, struct file *filp){
    printk(KERN_INFO "%s: Release operation, synchgroup device.\n", KBUILD_MODNAME);
	return 0;
}

ssize_t synchgroup_read (struct file *file, char __user *buf, size_t count, loff_t *offset){
    char *data = "Hello from the kernel world!\n";
    size_t datalen = strlen(data);
    struct list_head *ptr;
    struct list_head *ptr_message;
    struct group_dev *entry;
    struct message_t *message;
    struct list_head *ptr_message_to_del;
    
    //Offset will be used to keep track of the next message to be sent
    //Offset not needed, to be removed
    //TODO move in the right place
    printk("Synchgroup_read, offset before increment=%lld\n",*offset);
    *offset +=1;
    
    list_for_each(ptr,&group_list){
        entry=list_entry(ptr,struct group_dev, list);
        //Search in the list of group_dev
        if(entry->devt == file->f_path.dentry->d_inode->i_rdev){
            printk("Before list_first_entry\n");
            ptr_message = &entry->message.list;
            message = list_first_entry(ptr_message, struct message_t, list);
            printk("Synchgroup_read, message=%s\n",  *(&message->text));
            ptr_message_to_del = ptr_message->next;
            data = *(&message->text);
            
            if (count > datalen) {
                count = datalen;
            }

            if (copy_to_user(buf, data, count)) {
                return -EFAULT;
            }
            
            list_del(ptr_message_to_del);
            kfree(message);
        }
    }

    

    return count;
}

ssize_t synchgroup_write (struct file * file, const char __user *buf, size_t count, loff_t *offset){
    size_t maxdatalen = 30, ncopied;
    uint8_t databuf[maxdatalen];
    struct list_head *ptr;
    struct group_dev *entry;
    
    struct message_t *message;
    
    printk("Synchgroup_write, minor=%d\n", MINOR(file->f_path.dentry->d_inode->i_rdev));
    
    
    message = kmalloc(sizeof(*message),GFP_KERNEL);
    ncopied = copy_from_user(&message->text, buf, maxdatalen);
    
    list_for_each(ptr,&group_list){
        //Add the message in the list of messages the right group_dev
        entry=list_entry(ptr,struct group_dev, list);
        //Search in the list of group_dev
        if(entry->devt == file->f_path.dentry->d_inode->i_rdev){
            list_add_tail(&message->list,&entry->message.list);
        }
    }
    
    

    if (count < maxdatalen) {
        maxdatalen = count;
    }

    ncopied = copy_from_user(databuf, buf, maxdatalen);

    if (ncopied == 0) {
        printk("Copied %zd bytes from the user\n", maxdatalen);
    } else {
        printk("Could't copy %zd bytes from the user\n", ncopied);
    }

    databuf[maxdatalen] = 0;

    printk("Synchgroup_write, offset=%lld\n",*offset);
    printk("Data from the user: %s\n", message->text);

    return count;
}

long synchmess_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	ioctl_info info;
    char group_dev_name[32];
    char group_dev_file_name[32];
    struct file *file = NULL;
    struct group_dev *temp;
    struct list_head *ptr;
    struct group_dev *entry;

	switch (cmd) {
        case IOCTL_INSTALL_GROUP:
			printk("Called ioctl()\n");
			copy_from_user(&info, (ioctl_info *)arg, sizeof(ioctl_info));
			printk("Group name: %d\n", info.group_t);
            
            //Check if the group exists
            snprintf(group_dev_file_name,sizeof(group_dev_file_name),"/dev/synch/synchgroup_%d",info.group_t);
            printk("Group file name: %s\n", group_dev_file_name);
            file = filp_open(group_dev_file_name, O_RDONLY, 0);
            if(!IS_ERR(file)){
                filp_close(file, 0);
                // group exists
                memcpy(&info.file_path, group_dev_file_name, strlen(group_dev_file_name));
                copy_to_user((ioctl_info *)arg, &info, sizeof(ioctl_info));
            } else {
                // group doesn't exist
                //strcpy(group_dev_name, "synch!synchgroup_");
                //strcpy(group_dev_name, "synchgroup_");
                //strcat(group_dev_name, info.group_t);
                snprintf(group_dev_name,sizeof(group_dev_name),"synch!synchgroup_%d", info.group_t);
                // Create a device in the previously created class
                synchgroup_device = device_create(synchgroup_dev_cl, NULL, MKDEV(synchgroup_major, info.group_t), NULL, group_dev_name);
                if (IS_ERR(synchgroup_device)) {
                    printk(KERN_ERR "%s: failed to create device synchgroup\n", KBUILD_MODNAME);
                    ret = PTR_ERR(synchgroup_device);
                }
                printk(KERN_INFO "%s: special device synchgroup registered with major number %d\n", KBUILD_MODNAME, synchgroup_major);
                memcpy(&info.file_path, group_dev_file_name, strlen(group_dev_file_name));
                copy_to_user((ioctl_info *)arg, &info, sizeof(ioctl_info));
                
                temp = kmalloc(sizeof(*temp),GFP_KERNEL);
                snprintf(*(&temp->group_dev_name), sizeof(*(&temp->group_dev_name)), group_dev_name);
                temp->devt = MKDEV(synchgroup_major, info.group_t);
                list_add_tail(&temp->list,&group_list);
                
                //init the first element of message list in group_dev
                INIT_LIST_HEAD(&temp->message.list);
            }
            //only for logging purpose
            snprintf(group_dev_name,sizeof(group_dev_name),"synch!synchgroup_%d", info.group_t);
            
            //only for logging purpose
            list_for_each(ptr,&group_list){
                entry=list_entry(ptr,struct group_dev, list);
                printk(KERN_INFO "\n Hello %s  \n ", entry->group_dev_name);
            }
            
			goto out;
	}

    out:
	return ret;
}

int synchmess_open(struct inode *inode, struct file *filp) {
    printk(KERN_INFO "%s: Open operation, synchmess device.\n", KBUILD_MODNAME);
	return 0;
}


int synchmess_release(struct inode *inode, struct file *filp){
    printk(KERN_INFO "%s: Release operation, synchmess device.\n", KBUILD_MODNAME);
	return 0;
}

// Variables to correctly setup/shutdown the pseudo device file
static int synchmess_major;
static struct class *synchmess_dev_cl = NULL;
static struct device *synchmess_device = NULL;


static int __init synchmess_init(void){
    int err;
    
    INIT_LIST_HEAD(&group_list);
    
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
		printk(KERN_ERR "%s: failed to create device synchmess\n", KBUILD_MODNAME);
		err = PTR_ERR(synchmess_device);
		goto failed_devreg;
	}

	printk(KERN_INFO "%s: special device synchmess registered with major number %d\n", KBUILD_MODNAME, synchmess_major);

    printk(KERN_INFO "%s: Init registering dev files representing groups.\n", KBUILD_MODNAME);
    
	synchgroup_major = register_chrdev(0, "synchgroup", &synchgroup_fops);

	// Dynamically allocate a major for the synchgroup device
	if (synchgroup_major < 0) {
		printk(KERN_ERR "%s: Failed registering char device\n", KBUILD_MODNAME);
		err = synchgroup_major;
		goto failed_chrdevreg_synchgroup;
	}

	// Create a class for the synchgroup device
	synchgroup_dev_cl = class_create(THIS_MODULE, "synchgroup");
	if (IS_ERR(synchgroup_dev_cl)) {
		printk(KERN_ERR "%s: failed to register device class\n", KBUILD_MODNAME);
		err = PTR_ERR(synchgroup_dev_cl);
		goto failed_classreg_synchgroup;
	}

	return 0;

failed_classreg_synchgroup:
    unregister_chrdev(synchgroup_major, KBUILD_MODNAME);
failed_chrdevreg_synchgroup:
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
    struct list_head *ptr;
    struct list_head* tmp;
    struct group_dev *entry;
    printk(KERN_INFO "%s: Cleaning up module.\n", KBUILD_MODNAME);
    device_destroy(synchmess_dev_cl, MKDEV(synchmess_major, 0));
	class_unregister(synchmess_dev_cl);
	class_destroy(synchmess_dev_cl);
	unregister_chrdev(synchmess_major, KBUILD_MODNAME);
    list_for_each(ptr,&group_list){
        entry=list_entry(ptr,struct group_dev, list);
        device_destroy(synchgroup_dev_cl, entry->devt);
    }
	class_unregister(synchgroup_dev_cl);
	class_destroy(synchgroup_dev_cl);
    list_for_each_safe(ptr, tmp, &group_list){
        entry=list_entry(ptr,struct group_dev, list);
        unregister_chrdev(synchgroup_major, entry->group_dev_name);
        list_del(ptr);
        kfree(entry);
    }
	
}

module_init(synchmess_init);
module_exit(synchmess_cleanup);
