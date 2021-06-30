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
#include <linux/delay.h>
#include <linux/sched.h>

#include "synchmess-ioctl.h"

MODULE_AUTHOR("Daniele Pasquini <pasqdaniele@gmail.com>");
MODULE_DESCRIPTION("Thread Synchronization and Messaging Subsystem");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");

static int max_message_size = 50;
module_param(max_message_size,int,S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
MODULE_PARM_DESC(max_message_size,"The maximum size (bytes) currently allowed for posting messages to the device file");

static int max_storage_size = 500;
module_param(max_storage_size,int,S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
MODULE_PARM_DESC(max_storage_size,"The maximum number of bytes globally allowed for keeping messages in the device file");

//struct that contains a message
struct message_t {
    //body of the message
    char *text;
    //list of messages it belongs to
    struct list_head list;
};

//struct that contains data for each write
struct delayed_work_params {
    //delayed_work to execute a write
    struct delayed_work my_work;
    //body of the message to be written
    char *text_message;
    //minor to identify the group to write in
    int minor;
    //list of delayed_work_params it belongs to
    struct list_head list;
};

//struct that contains info for each group
struct group_dev {
    //device number
	dev_t devt;
    //group name
    char group_dev_name[32];
    //list of groups
    struct list_head list;
    //list of messages
    struct list_head message_list;
    //lock to access a group
    struct mutex group_lock;
    //timeout to manage write delay
    unsigned long timeout_millis;
    //workqueue to execute the write
    struct workqueue_struct *wq;
    //list of delayed works, each one contains a message to be written
    struct list_head delayed_work_param_list;
    //wait_queue to manage sleep on and awake barrier
    wait_queue_head_t sleep_queue;
};

//list of groups
struct list_head group_list;

long synchmess_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
int synchmess_open(struct inode *inode, struct file *filp);
int synchmess_release(struct inode *inode, struct file *filp);

long synchgroup_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
int synchgroup_open(struct inode *inode, struct file *filp);
int synchgroup_release(struct inode *inode, struct file *filp);
ssize_t synchgroup_read (struct file *file, char __user *buf, size_t count, loff_t *offset);
ssize_t synchgroup_write (struct file *file, const char __user *buf, size_t count, loff_t *offset);
int synchgroup_flush (struct file *file, fl_owner_t id);

//File operations for the device synchmess
//synchmess is the device that allows a client to create a group
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
    write: synchgroup_write,
    flush: synchgroup_flush
};

//counter that keeps the last group minor
static atomic_t groups_number;

// Variables to correctly setup/shutdown the pseudo device file for synchgroup
static int synchgroup_major;
static struct class *synchgroup_dev_cl = NULL;

//to make all the messages with delay immediately available to subsequent read calls
int synchgroup_flush (struct file *file, fl_owner_t id){
    struct list_head *ptr;
    struct group_dev *entry;
    struct list_head *ptr_delayed_work_params;
    struct list_head *tmp_delayed_work_params;
    struct delayed_work_params *entry_params;
    
    printk(KERN_INFO "%s: flush operation, synchgroup device minor: %d.\n", KBUILD_MODNAME, MINOR(file->f_path.dentry->d_inode->i_rdev));
    
    list_for_each(ptr,&group_list){
        entry=list_entry(ptr,struct group_dev, list);
        //Search the right group in the list of group_dev
        if(entry->devt == file->f_path.dentry->d_inode->i_rdev){
            //to enable concurrent access
            mutex_lock_interruptible(&entry->group_lock);
            
            //for each delayed work params
            list_for_each_safe(ptr_delayed_work_params, tmp_delayed_work_params, &entry->delayed_work_param_list){
                entry_params = list_entry(ptr_delayed_work_params ,struct delayed_work_params, list);
                //to call all the write calls that are waiting a delay
                mod_delayed_work(entry->wq, &entry_params->my_work, 0);
            }
            
            mutex_unlock(&entry->group_lock);
        }    
    }
    return 0;
}

//file operation to manage operations on groups(SET_SEND_DELAY, REVOKE_DELAYED_MESSAGES, SLEEP_ON_BARRIER, AWAKE_BARRIER)
long synchgroup_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){
	long ret = 0;
    //struct to exchange data with the client
    ioctl_info info;
    struct list_head *ptr;
    struct group_dev *entry;
    struct list_head *ptr_delayed_work_params;
    struct list_head *tmp_delayed_work_params;
    struct delayed_work_params *entry_params;
    wait_queue_t wait;

	switch (cmd) {
        case SET_SEND_DELAY:
            printk(KERN_INFO "%s: SET SEND DELAY operation, synchgroup device minor: %d.\n", KBUILD_MODNAME, MINOR(filp->f_path.dentry->d_inode->i_rdev));
            copy_from_user(&info, (ioctl_info *)arg, sizeof(ioctl_info));
            list_for_each(ptr,&group_list){
                entry=list_entry(ptr,struct group_dev, list);
                //Search the right group in the list of group_dev
                if(entry->devt == filp->f_path.dentry->d_inode->i_rdev){
                    entry->timeout_millis = info.timeout_millis;
                }    
            }
			goto out_ioctl;
            
        case REVOKE_DELAYED_MESSAGES:
            printk(KERN_INFO "%s: REVOKE DELAYED MESSAGES operation, synchgroup device minor: %d.\n", KBUILD_MODNAME, MINOR(filp->f_path.dentry->d_inode->i_rdev));
            list_for_each(ptr,&group_list){
                entry=list_entry(ptr,struct group_dev, list);
                //Search the right group in the list of group_dev
                if(entry->devt == filp->f_path.dentry->d_inode->i_rdev){
                    //to enable concurrent access
                    mutex_lock_interruptible(&entry->group_lock);
                    
                    //for each delayed work for the selected group
                    list_for_each_safe(ptr_delayed_work_params, tmp_delayed_work_params, &entry->delayed_work_param_list){
                        entry_params = list_entry(ptr_delayed_work_params ,struct delayed_work_params, list);
                        //remove delayed work from the list
                        cancel_delayed_work_sync(&entry_params->my_work);
                        list_del(ptr_delayed_work_params);
                        kfree(entry_params->text_message);
                        kfree(entry_params);
                    }
                    
                    mutex_unlock(&entry->group_lock);
                }    
            }
			goto out_ioctl;
            
        case SLEEP_ON_BARRIER:
            printk(KERN_INFO "%s: SLEEP ON BARRIER operation, synchgroup device minor: %d.\n", KBUILD_MODNAME, MINOR(filp->f_path.dentry->d_inode->i_rdev));
            list_for_each(ptr,&group_list){
                entry=list_entry(ptr,struct group_dev, list);
                //Search the right group in the list of group_dev
                if(entry->devt == filp->f_path.dentry->d_inode->i_rdev){
                    //init a wait queue entry to manage sleep on barrier
                    init_waitqueue_entry(&wait, current);
                    set_current_state(TASK_INTERRUPTIBLE);
                    
                    //add the wait queue entry to the wait queue of the group
                    add_wait_queue(&entry->sleep_queue, &wait);
                    //call schedule to deschedule the actual task until an AWAKE_BARRIER arrives
                    schedule();
                    //when an AWAKE_BARRIER arrives remove the wait queue entry from the queue
                    remove_wait_queue (&entry->sleep_queue, &wait);
                }    
            }
			goto out_ioctl;
            
        case AWAKE_BARRIER:
            printk(KERN_INFO "%s: AWAKE BARRIER operation, synchgroup device minor: %d.\n", KBUILD_MODNAME, MINOR(filp->f_path.dentry->d_inode->i_rdev));
            list_for_each(ptr,&group_list){
                entry=list_entry(ptr,struct group_dev, list);
                //Search the right group in the list of group_dev
                if(entry->devt == filp->f_path.dentry->d_inode->i_rdev){
                    //wake up all tasks in the sleep queue of the group
                    wake_up_all(&entry->sleep_queue);
                }    
            }
			goto out_ioctl;
	}

    out_ioctl:
	return ret;
}

int synchgroup_open(struct inode *inode, struct file *filp) {
    printk(KERN_INFO "%s: Open operation, synchgroup device minor: %d.\n", KBUILD_MODNAME, MINOR(filp->f_path.dentry->d_inode->i_rdev));
	return 0;
}


int synchgroup_release(struct inode *inode, struct file *filp){
    printk(KERN_INFO "%s: Release operation, synchgroup device minor: %d.\n", KBUILD_MODNAME, MINOR(filp->f_path.dentry->d_inode->i_rdev));
	return 0;
}

ssize_t synchgroup_read (struct file *file, char __user *buf, size_t count, loff_t *offset){
    char *data;
    //to scan the list of groups
    struct list_head *ptr;
    //to scan the list of messages
    struct list_head *ptr_message;
    struct group_dev *entry;
    struct message_t *message;
    struct list_head *ptr_message_to_del;
    
    printk(KERN_INFO "%s: read operation, synchgroup device minor: %d.\n", KBUILD_MODNAME, MINOR(file->f_path.dentry->d_inode->i_rdev));
    
    list_for_each(ptr,&group_list){
        entry=list_entry(ptr,struct group_dev, list);
        //Search the right group in the list of group_dev
        if(entry->devt == file->f_path.dentry->d_inode->i_rdev){
            //to enable concurrent access
            mutex_lock_interruptible(&entry->group_lock);
            
            ptr_message = &entry->message_list;
            if(list_empty(ptr_message)){
                //if the list is empty there are no messages to read
                count = 0;
                printk(KERN_INFO "%s: List is empty\n", KBUILD_MODNAME);
                mutex_unlock(&entry->group_lock);
                return count;
            }
            //get the first message to read
            message = list_first_entry(ptr_message, struct message_t, list);
            printk(KERN_INFO "%s: Synchgroup_read, device minor: %d, message=%s\n", KBUILD_MODNAME, MINOR(file->f_path.dentry->d_inode->i_rdev), *(&message->text));
            
            //to delete the message read
            ptr_message_to_del = ptr_message->next;
            data = *(&message->text);
            
            //count is the max number of bytes the client wants to read
            if (count > strlen(data)) {
                //if count is bigger than the length of the message it will be cut
                count = strlen(data);
            }

            //copy message to the user
            if (copy_to_user(buf, data, count)) {
                return -EFAULT;
            }
            
            //remove the actual message from the list
            list_del(ptr_message_to_del);
            kfree(message->text);
            kfree(message);
            
            mutex_unlock(&entry->group_lock);
        }
    }

    return count;
}

/*Workqueue Function*/
static void workqueue_write(struct work_struct *work){
    struct delayed_work_params *params;
    //to scan the list of groups
    struct list_head *ptr;
    //to scan the list of messages
    struct list_head *ptr_message;
    struct group_dev *entry;
    struct message_t *message;
    struct message_t *entry_message;
    size_t counter_bytes = 0;

    params = container_of(work, struct delayed_work_params,  my_work);
    printk(KERN_INFO "%s: workqueue_write: Executing Workqueue Function, params.minor = %d\n", KBUILD_MODNAME, params->minor);
    
    message = kmalloc(sizeof(*message),GFP_KERNEL);
    message->text = kmalloc(strlen(params->text_message),GFP_KERNEL);
    
    //copy the body of the message from params
    strcpy(message->text, params->text_message);

    list_for_each(ptr,&group_list){
        //Add the message in the list of messages the right group_dev
        entry = list_entry(ptr,struct group_dev, list);

        //Search the right group in the list of group_dev
        if(params->minor == MINOR(entry->devt)){
            
            //to enable concurrent access
            mutex_lock_interruptible(&entry->group_lock);
            
            //printk("workqueue_write: After mutex_lock\n");
            
            //loop to check if max_storage_size is reached
            list_for_each(ptr_message,&entry->message_list){
                entry_message = list_entry(ptr_message,struct message_t, list);
                counter_bytes += strlen(*(&entry_message->text));
            }
            //printk(KERN_INFO "%s: workqueue_write: counter = %zd\n", KBUILD_MODNAME, counter_bytes);
            counter_bytes += strlen(params->text_message);
            if(counter_bytes > max_storage_size){
                printk(KERN_ERR "%s: workqueue_write: Maximum storage size reached\n", KBUILD_MODNAME);
                return;
            }
            
            //if there is space enough, add the message to the list
            list_add_tail(&message->list,&entry->message_list);
            
            
            printk(KERN_INFO "%s: workqueue_write: params.minor = %d, message = %s\n", KBUILD_MODNAME, params->minor, params->text_message);
            //remove the actual delayed_work_params from the list
            list_del(&params->list);
            kfree(params->text_message);
            kfree(params);
            
            mutex_unlock(&entry->group_lock);
        }
    }
    return;
}

ssize_t synchgroup_write (struct file * file, const char __user *buf, size_t count, loff_t *offset){
    size_t maxdatalen = max_message_size; 
    size_t ncopied = 0;
    //to scan the list of groups
    struct list_head *ptr;
    struct group_dev *entry;
    
    //params for the delayed work that will do the write
    struct delayed_work_params *params;
    
    printk(KERN_INFO "%s: Synchgroup_write, minor=%d\n", KBUILD_MODNAME, MINOR(file->f_path.dentry->d_inode->i_rdev));
    
    //count is the number of bytes the client wants to read
    if (count < maxdatalen) {
        maxdatalen = count;
    }
    
    
    list_for_each(ptr,&group_list){
        //Add the work that will write the message in the right group_dev
        entry = list_entry(ptr,struct group_dev, list);
        //Search the right group in the list of group_dev
        if(entry->devt == file->f_path.dentry->d_inode->i_rdev){
            params = kmalloc(sizeof(*params),GFP_KERNEL);
            params->text_message = kmalloc(maxdatalen,GFP_KERNEL);
            //minor of the dev representing the group
            params->minor = MINOR(file->f_path.dentry->d_inode->i_rdev);
            //number of bytes copied from the user 
            ncopied = copy_from_user(params->text_message, buf, maxdatalen);
            params->text_message[maxdatalen] = 0;
            //worqueue_write is the function that will do the write of the message
            INIT_DELAYED_WORK(&params->my_work, workqueue_write);
            
            //to enable concurrent access
            mutex_lock_interruptible(&entry->group_lock);
            list_add_tail(&params->list, &entry->delayed_work_param_list);
            mutex_unlock(&entry->group_lock);
            //add the delayed work to the workqueue with the timeout of the group
            queue_delayed_work(entry->wq, &params->my_work, msecs_to_jiffies(entry->timeout_millis));
        }
    }


    if (ncopied == 0) {
        printk(KERN_INFO "%s: Copied %zd bytes from the user\n", KBUILD_MODNAME, maxdatalen);
    } else {
        printk(KERN_INFO "%s: Could't copy %zd bytes from the user\n", KBUILD_MODNAME, ncopied);
    }

    printk(KERN_INFO "%s: Data from the user: %s\n", KBUILD_MODNAME, buf);

    return maxdatalen;
}

//file operation to manage the creation of a group
long synchmess_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
    //struct to exchange data with the client
	ioctl_info info;
    struct device *synchgroup_device;
    char group_dev_name[32];
    char group_dev_file_name[32];
    struct file *file = NULL;
    struct group_dev *temp;
    char wq_name[32];
    int next_minor;
    

	switch (cmd) {
        case IOCTL_INSTALL_GROUP:
			printk(KERN_INFO "%s: IOCTL INSTALL GROUP operation, synchmess device.\n", KBUILD_MODNAME);
            
			copy_from_user(&info, (ioctl_info *)arg, sizeof(ioctl_info));
			printk(KERN_INFO "%s: IOCTL INSTALL GROUP operation, Group name: %s\n", KBUILD_MODNAME, info.group.name);
            
            //Check if the group exists
            snprintf(group_dev_file_name,sizeof(group_dev_file_name),"/dev/synch/synchgroup_%s",info.group.name);

            //try to open the file associated with the group
            file = filp_open(group_dev_file_name, O_RDONLY, 0);
            if(!IS_ERR(file)){//if the group exists
                filp_close(file, 0);
                //copy the file path of the file associated with the group in info
                strcpy(info.file_path, group_dev_file_name);
                //copy_to_user to make the file path available to the client
                copy_to_user((ioctl_info *)arg, &info, sizeof(ioctl_info));
            } else { // group doesn't exist
                
                snprintf(group_dev_name,sizeof(group_dev_name),"synch!synchgroup_%s", info.group.name);
                // Create a device in the previously created class
                
                //minor for the new group is groups_number +1
                next_minor = atomic_inc_return(&groups_number);
                printk(KERN_INFO "%s: next_minor = %d\n", KBUILD_MODNAME, next_minor);
                synchgroup_device = device_create(synchgroup_dev_cl, NULL, MKDEV(synchgroup_major, next_minor), NULL, group_dev_name);
                if (IS_ERR(synchgroup_device)) {
                    printk(KERN_ERR "%s: failed to create device synchgroup\n", KBUILD_MODNAME);
                    ret = PTR_ERR(synchgroup_device);
                }
                printk(KERN_INFO "%s: special device synchgroup registered with major number %d\n", KBUILD_MODNAME, synchgroup_major);
                //copy the file path of the file associated with the group in info
                strcpy(info.file_path, group_dev_file_name);
                copy_to_user((ioctl_info *)arg, &info, sizeof(ioctl_info));
                
                //create a group_dev to be inserted in the list of groups
                temp = kmalloc(sizeof(*temp),GFP_KERNEL);
                //group name
                snprintf(*(&temp->group_dev_name), sizeof(*(&temp->group_dev_name)), group_dev_name);
                //device number
                temp->devt = MKDEV(synchgroup_major, next_minor);
                //default timeout for a group
                temp->timeout_millis = 0;
                //add the group to the list of group
                list_add_tail(&temp->list,&group_list);
                
                //init the first element of message list in the group
                INIT_LIST_HEAD(&temp->message_list);
                
                //init the first element of delayed_work_param list in the group
                INIT_LIST_HEAD(&temp->delayed_work_param_list);
                
                //init the mutex to access the group
                mutex_init(&temp->group_lock);
                
                //name of the workqueue is the device group name
                snprintf(wq_name,sizeof(wq_name),"synchgroup_%s",info.group.name);
                //create the workqueue where delayed work will be executed
                temp->wq = create_workqueue(wq_name);
                
                //init the wait queue to manage sleep on and awake barrier
                init_waitqueue_head (&temp->sleep_queue);
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

// Variables to correctly setup/shutdown the pseudo device file for synchmess
static int synchmess_major;
static struct class *synchmess_dev_cl = NULL;
static struct device *synchmess_device = NULL;


static int __init synchmess_init(void){
    int err;
    
    //init list of groups
    INIT_LIST_HEAD(&group_list);
    
    printk(KERN_INFO "%s: Init module.\n", KBUILD_MODNAME);
    
    // Dynamically allocate a major for the synchmess device
	synchmess_major = register_chrdev(0, KBUILD_MODNAME, &synchmess_fops);
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
    
    // Dynamically allocate a major for the synchgroup device
	synchgroup_major = register_chrdev(0, "synchgroup", &synchgroup_fops);
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
    
    printk(KERN_INFO "%s: special device synchgroup registered with major number %d\n", KBUILD_MODNAME, synchgroup_major);

    //init groups_number with 0
    atomic_set(&groups_number, 0);
    
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
    struct list_head *ptr_message;
    struct list_head* tmp_message;
    struct group_dev *entry;
    struct message_t *entry_message;
    struct list_head *ptr_delayed_work_params;
    struct list_head *tmp_delayed_work_params;
    struct delayed_work_params *entry_params;
    
    printk(KERN_INFO "%s: Cleaning up module.\n", KBUILD_MODNAME);
    
    //destroy and unregister synchmess device
    device_destroy(synchmess_dev_cl, MKDEV(synchmess_major, 0));
	class_unregister(synchmess_dev_cl);
	class_destroy(synchmess_dev_cl);
	unregister_chrdev(synchmess_major, KBUILD_MODNAME);
    
    //for each group
    list_for_each_safe(ptr, tmp, &group_list){
        entry = list_entry(ptr,struct group_dev, list);
        
        //to enable concurrent access
        mutex_lock_interruptible(&entry->group_lock);
        
        //destroy the device associated with the group
        device_destroy(synchgroup_dev_cl, entry->devt);
        
        //for each message in the group
        list_for_each_safe(ptr_message, tmp_message, &entry->message_list){
            entry_message = list_entry(ptr_message ,struct message_t, list);
            //remove message from the list
            list_del(ptr_message);
            //free memory
            kfree(entry_message);
        }
        
        //for each delayed work params
        list_for_each_safe(ptr_delayed_work_params, tmp_delayed_work_params, &entry->delayed_work_param_list){
            entry_params = list_entry(ptr_delayed_work_params ,struct delayed_work_params, list);
            cancel_delayed_work_sync(&entry_params->my_work);
            //remove dealyed work params from the list
            list_del(ptr_delayed_work_params);
            //free memory
            kfree(entry_params->text_message);
            kfree(entry_params);
        }
        
        destroy_workqueue(entry->wq);
        
        mutex_unlock(&entry->group_lock);
        
        //remove the group from the list
        list_del(ptr);
        //free memory
        kfree(entry);
    }
    //unregister and destroy the class associated with synchgroup
    class_unregister(synchgroup_dev_cl);
	class_destroy(synchgroup_dev_cl);
    //unregister the device associated with the group
    unregister_chrdev(synchgroup_major, 0);
    
	printk(KERN_INFO "%s: Cleaning completed.\n", KBUILD_MODNAME);
}

module_init(synchmess_init);
module_exit(synchmess_cleanup);
