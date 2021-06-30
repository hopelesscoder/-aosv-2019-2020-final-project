#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <pthread.h>

#include "synchmess-ioctl.h"

//compile with -pthread
//this is a userspace application to test SLEEP_ON_BARRIER and AWAKE

int fd_group;

void* doSleep(void *arg)
{
    int a = *((int *) arg);
    printf("In doSleep thread num=%d\n", a);
    printf("SLEEP_ON_BARRIER called, thread num=%d.\n", a);
    ioctl(fd_group, SLEEP_ON_BARRIER);
    
    printf("After thread awake, thread num=%d.\n", a);

    free(arg);

    return NULL;
}

int main(void) {
    ioctl_info info;
    
	int fd = open("/dev/synchmess", O_RDONLY);

	if(fd < 0) {
		perror("Error opening /dev/synchmess");
		exit(EXIT_FAILURE);
	}

    snprintf(info.group.name,sizeof(info.group.name),"first");
	ioctl(fd, IOCTL_INSTALL_GROUP, &info);
    printf("%s\n", info.file_path);
    
    fd_group = open(info.file_path, O_RDWR);
    
    pthread_t tid;
    int i = 0;
    int err[2];

    while(i < 2) {
        int *arg = malloc(sizeof(*arg));
        if ( arg == NULL ) {
            fprintf(stderr, "Couldn't allocate memory for thread arg.\n");
            exit(EXIT_FAILURE);
        }

        //to know the number of thread
        *arg = i;
        err[i] = pthread_create(&tid, NULL, &doSleep, arg);
        if (err[i] != 0)
            printf("\ncan't create thread :[%s]", strerror(err[i]));
        else
            printf("Thread num %d created successfully\n", i);
        sleep(4);
        i++;
    }
    
    //wait threads to call SLEEP_ON_BARRIER
    sleep(10);
    
    printf("Before AWAKE_BARRIER, in main.\n");
    ioctl(fd_group, AWAKE_BARRIER);
    
    sleep(10);
    
    pthread_join(err[0], NULL);
    pthread_join(err[1], NULL);
    
    close(fd_group);
    close(fd);
    
	return 0;

}

