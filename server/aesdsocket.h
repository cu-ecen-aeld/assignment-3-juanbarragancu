/** 
 * This header file is used for connecting to a client using sockets
 * and reading and sending data back and forth to the client.
 * 
 * @author Juan Barragan
 */
#ifndef __AESDOCKET_H__
#define __AESDOCKET_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <syslog.h>
#include <signal.h>
#include <stdbool.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include "queue.h"

#define FILE_ADDR "/var/tmp/aesdsocketdata" //filepath of where the file is located
#define BACKLOG 10 //argument for listen()
#define INTITAL_ALARM 10 //when should the first alarm go off
#define INTERVAL 10 //how many seconds the susbequents timers should go off
#define KILOBYTE 1024 //size of buffer
#define OFF 1 //we're turning off the timer
#define WRITE 1 //we're writing to file
#define SEND 0 //sending contents back to client


/**
 * This structure should be dynamically allocated and passed as
 * an argument to your thread using pthread_create.
 */
struct thread_data{
    pthread_t thread; //store thread id
    pthread_mutex_t *mutex; //mutex for individual thread
    int fd; //file descriptor for each worker thread
    int client_socket_fd; //client socket connection for each worker thread
    char client_ip[NI_MAXHOST]; //client IP address for each worker thread
    bool thread_finished; //did the thread finish its implementation
    SLIST_ENTRY(thread_data) entries; //used for link list implementation
};


static void signal_handler (int signal_number);
static void cleanup_log_statement(int priority, char *msg, int sockfd, struct addrinfo *servinfo);
static int packet_work(int fd, char* data, size_t length, int flag);
static void timer_function(union sigval sigval);
static void cleanup_worker_thread_and_data(void* thread_param, char *data);
static void *receive_send_client(void *thread);

#endif /* __AESDOCKET_H__ */