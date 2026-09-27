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

#define FILE_ADDR "/var/tmp/aesdsocketdata"
#define BACKLOG 10
#define KILOBYTE 1024
#define WRITE 1
#define SEND 0

static void signal_handler (int signal_number);
static void cleanup_log_statement(int priority, char *msg, int sockfd, struct addrinfo *servinfo);
static int packet_work(int fd, char* data, size_t length, int flag);
static void receive_send_client(int fd, int clientSockfd);