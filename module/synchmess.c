#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h> 

MODULE_AUTHOR("Daniele Pasquini <pasqdaniele@gmail.com>");
MODULE_DESCRIPTION("Thread Synchronization and Messaging Subsystem");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");

static int __init synchmess_init(void){
    printk(KBUILD_MODNAME ": Init module.\n");
	return 0;
}

static void __exit synchmess_cleanup(void)
{
    printk(KBUILD_MODNAME ": Cleaning up module.\n");
}

module_init(synchmess_init);
module_exit(synchmess_cleanup);
