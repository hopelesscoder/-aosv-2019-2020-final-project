#pragma once

#include <linux/ioctl.h>

#define MYDEV_IOC_MAGIC 'R'

typedef struct _group_t {
	char name[16];
} group_t;

#define IOCTL_INSTALL_GROUP	 		_IOR(MYDEV_IOC_MAGIC, 1, group_t *)
