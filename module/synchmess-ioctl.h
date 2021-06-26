#pragma once

#include <linux/ioctl.h>

#define MYDEV_IOC_MAGIC 'R'

typedef struct _group_t {
	unsigned int name;
} group_t;

typedef struct _ioctl_info {
	group_t group;
    char file_path[32];
    unsigned long timeout_millis;
} ioctl_info;

#define IOCTL_INSTALL_GROUP	 		_IOW(MYDEV_IOC_MAGIC, 1, ioctl_info *)
#define SET_SEND_DELAY              _IOW(MYDEV_IOC_MAGIC, 2, ioctl_info *)
#define REVOKE_DELAYED_MESSAGES     _IO(MYDEV_IOC_MAGIC, 3)
#define SLEEP_ON_BARRIER            _IO(MYDEV_IOC_MAGIC, 4)
#define AWAKE_BARRIER               _IO(MYDEV_IOC_MAGIC, 5)
