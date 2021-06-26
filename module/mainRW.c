#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include "synchmess-ioctl.h"

//compile with -pthread
//this is a userspace application to test write, read, SET_SEND_DELAY, REVOKE_DELAYED_MESSAGES and flush

ioctl_info info;


void* write_thread(void *arg)
{
    int fd_group = *((int *) arg);
    printf("In write\n");
    int numBytes = write(fd_group, "First write", strlen("First write"));
    printf("written %d bytes from first write \n", numBytes);
    numBytes = write(fd_group, "Second write", strlen("Second write"));
    printf("written %d bytes from second write \n", numBytes);

    return NULL;
}


void* read_thread(void *arg)
{
    char buf[50];
    char buf1[50];
    char buf2[50];
    int fd_group = *((int *) arg);
    printf("In read\n");
    ssize_t size = read(fd_group, buf, 50);
    if(size == 0){
        printf("Message not found; \n"); 
    }else{
        buf[size] = '\0';
        printf("First Read, %s; \n", buf); 
    }
    
    ssize_t size1 = read(fd_group, buf1, 50);
    if(size1 == 0){
        printf("Message not found; \n"); 
    }else{
        buf1[size1] = '\0';
        printf("Second Read, %s; \n", buf1); 
    }
    
    ssize_t size2 = read(fd_group, buf2, 50);
    if(size2 == 0){
        printf("Message not found; \n"); 
    }else{
        buf1[size2] = '\0';
        printf("Third Read, %s; \n", buf2); 
    }

    return NULL;
}

int main(void) {
    int fd = open("/dev/synchmess", O_RDONLY);

	if(fd < 0) {
		perror("Error opening /dev/synchmess");
		exit(EXIT_FAILURE);
	}
    
    info.group.name = 1;
	ioctl(fd, IOCTL_INSTALL_GROUP, &info);
    printf("file path %s\n", info.file_path);
    
    int fd_group = open(info.file_path, O_RDWR);
    
    int *arg1 = malloc(sizeof(*arg1));
    if ( arg1 == NULL ) {
        fprintf(stderr, "Couldn't allocate memory for thread arg.\n");
        exit(EXIT_FAILURE);
    }

    *arg1 = fd_group;   
    
    printf("\n:::now we try the first write/read:::\n");
    
    pthread_t tid_write;
    int err_write = pthread_create(&tid_write, NULL, &write_thread, arg1);
    if (err_write != 0)
        printf("\ncan't create thread :[%s]", strerror(err_write));
    else
        printf("Thread write created successfully\n");
    
    sleep(5);
    
    int *arg2 = malloc(sizeof(*arg2));
    if ( arg2 == NULL ) {
        fprintf(stderr, "Couldn't allocate memory for thread arg.\n");
        exit(EXIT_FAILURE);
    }

    *arg2 = fd_group;   
    pthread_t tid_read;
    int err_read = pthread_create(&tid_read, NULL, &read_thread, arg2);
    if (err_read != 0)
        printf("\ncan't create thread :[%s]", strerror(err_read));
    else
        printf("Thread read created successfully\n");
    
    
    sleep(1);
    
    pthread_join(err_write, NULL);
    pthread_join(err_read, NULL);
    
    
    //SET_SEND_DELAY 30000 msec = 30 seconds
    info.group.name = 1;
    info.timeout_millis = 120000;
	ioctl(fd_group, SET_SEND_DELAY, &info);
    printf("\n:::now we set send delay to 30 seconds and then write/read:::\n");
    
    *arg1 = fd_group;   
    
    err_write = pthread_create(&tid_write, NULL, &write_thread, arg1);
    if (err_write != 0)
        printf("\ncan't create thread :[%s]", strerror(err_write));
    else
        printf("Thread write created successfully\n");
    
    sleep(5);

    *arg2 = fd_group;   
    err_read = pthread_create(&tid_read, NULL, &read_thread, arg2);
    if (err_read != 0)
        printf("\ncan't create thread :[%s]", strerror(err_read));
    else
        printf("Thread read created successfully\n");
    
    sleep(1);
    
    pthread_join(err_write, NULL);
    pthread_join(err_read, NULL);
    
    
    //REVOKE_DELAYED_MESSAGES
	ioctl(fd_group, REVOKE_DELAYED_MESSAGES);
    printf("\n:::now we revoke delayed messages, wait 30 seconds and read:::\n");
    
    sleep(30);
    
    //read doesn't find anything
    err_read = pthread_create(&tid_read, NULL, &read_thread, arg2);
    if (err_read != 0)
        printf("\ncan't create thread :[%s]", strerror(err_read));
    else
        printf("Thread read created successfully\n");
    
    sleep(1);
    
    pthread_join(err_read, NULL);
    
    
    printf("\n:::now we call write:::\n");
    err_write = pthread_create(&tid_write, NULL, &write_thread, arg1);
    if (err_write != 0)
        printf("\ncan't create thread :[%s]", strerror(err_write));
    else
        printf("Thread write created successfully\n");
    
    sleep(5);
    
    pthread_join(err_write, NULL);
    
    
    //flush and read
    close(fd_group);
    printf("\n:::now we call flush(doing close and open) and then read:::\n");
    sleep(5);
    
    fd_group = open(info.file_path, O_RDWR);
    *arg2 = fd_group;  
    err_read = pthread_create(&tid_read, NULL, &read_thread, arg2);
    if (err_read != 0)
        printf("\ncan't create thread :[%s]", strerror(err_read));
    else
        printf("Thread read created successfully\n");
    sleep(1);
    
    pthread_join(err_read, NULL);
    
    
    close(fd_group);
    close(fd);
    free(arg1);
    free(arg2);
    
	return 0;

}
