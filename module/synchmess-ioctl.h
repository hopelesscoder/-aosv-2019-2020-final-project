#pragma once

#include <linux/ioctl.h>

#define MYDEV_IOC_MAGIC 'R'

typedef struct _ioctl_info {
	unsigned int group_t;
    char file_path[32];
} ioctl_info;

/*typedef struct _group_t {
	unsigned int name;
} group_t;*/

#define IOCTL_INSTALL_GROUP	 		_IOW(MYDEV_IOC_MAGIC, 1, ioctl_info *)
