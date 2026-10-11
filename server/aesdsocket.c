/** 
 * This program is used for connecting to a client using sockets
 * and reading and sending data back and forth to the client.
 * 
 * @author Juan Barragan
 */

#include "aesdsocket.h"

bool graceful_exit = false; //flag set by signal handler

/**
 * Signal handler for the SIGINT and SIGTERM signals
 * received.
 *
 * @param signal_number the signal that was received
 *
 */
static void signal_handler (int signal_number)
{
    if(signal_number == SIGINT || signal_number == SIGTERM)
    {
        graceful_exit = true;
    }
}

/**
 * Logs errors and releases resources during server set up
 *
 * @param priority level of the syslog
 * @param msg syslog message to be written
 * @param sockfd server socket to be closed
 * @param servinfo addrinfo struct to be freed
 *
 */
static void cleanup_log_statement(int priority, char *msg, int sockfd, struct addrinfo *servinfo)
{
    syslog(priority, "%s", msg);
    close(sockfd);
    freeaddrinfo(servinfo);
}

/**
 * Function that writes the data received from the client to a file
 * or sends the written data from a file to the client.
 *
 * @param fd file descriptor of the file to be worked
 * @param data data received from the client or data to be sent
 *              to the client
 * @param length length of the data to be worked
 * @param flag value that determines if we're writing to a file
 *              or sending data to the client
 *
 */
static int packet_work(int fd, char* data, size_t length, int flag)
{
    size_t data_worked = 0;
    ssize_t ret;

    while (data_worked < length) 
    {
        if(flag == WRITE)
        {
            ret = write(fd, (data + data_worked), (length - data_worked));
        }
        else if(flag == SEND)
        {
            ret = send(fd, (data + data_worked), (length - data_worked), 0);
        }
        else
        {
            //return error because the given flag is unknown
            return -1;    
        }

        if (ret <= 0) 
        {
            /* return due to error from either write() 
            or send() */
            return -1;
        }

        data_worked += (size_t)ret;
    }
    
    return 0;
}   

/**
 * Timer function that appends the current time to the file when called
 *
 * @param sigval what signal occurred and state of the system when it occurred
 */
static void timer_function(union sigval sigval)
{
    struct thread_data *timer_thread = (struct thread_data*) sigval.sival_ptr;
    time_t t;
    struct tm *local_time;
    char curr_time[KILOBYTE];
 
    time(&t); //return current time
    local_time = localtime(&t); //format time using localtime

    //need the number of bytes in the result string to know how much we're going to write
    size_t time_len = strftime(curr_time, sizeof(curr_time), "timestamp:%a, %d %b %Y %T %z\n", local_time);

    //nothing was written so exit
    if(time_len == 0)
    {
        return;
    }

    if(pthread_mutex_lock(timer_thread->mutex) != 0)
    {
        syslog(LOG_ERR,"mutex lock failed at timer_function()\n");
        return;
    }
    else
    {
        int fd = open(FILE_ADDR, O_RDWR | O_CREAT | O_APPEND, 0664);
        if (fd != -1)
        {
            //append timestamp to file
            if(packet_work(fd, curr_time, time_len, WRITE) != 0)
            {
                close(fd);
                syslog(LOG_ERR, "write() failed");
                if(pthread_mutex_unlock(timer_thread->mutex) != 0)
                {
                    syslog(LOG_ERR, "mutex unlock failed at timer_function()\n");
                }
                return;
            }
            close(fd);
        }
        if(pthread_mutex_unlock(timer_thread->mutex) != 0)
        {
            syslog(LOG_ERR,"mutex unlock failed at timer_function()\n");
        }
    }
    
    return;
}

/**
 * Overall process that cleans up a worker thread and client
 * on exit for any reason
 *
 * @param thread_param thread struct that contains all data that
 * will be needed to send back and forth client recieved data
 * for eack worker thread
 * @param data data recieved from client
 */
static void cleanup_worker_thread_and_data(void* thread_param, char *data)
{
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    
    //close client connection
    if(close(thread_func_args->client_socket_fd) != -1)
    {
        syslog(LOG_DEBUG, "Closed connection from %s", thread_func_args->client_ip);
    }
    
    //close file descriptor 
    close(thread_func_args->fd);
    
    //thread has finished its functionality
    thread_func_args->thread_finished = true;
    
    //free client data if there has been some recorded
    if(data != NULL)
    {
        free(data);
    }
}

/**
 * Overall process that performs writing received data from a client
 * and sending data back to the client after the binding process
 * has succeeded.
 *
 * @param thread_param thread struct that contains all data that
 * will be needed to send back and forth client recieved data
 * for eack worker thread
 */
static void *receive_send_client(void* thread_param)
{
    char *data = NULL; //client data
    size_t data_len = 0; //length of client data
    char recv_buffer[KILOBYTE]; //buffer to hold received data
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    while(true)
    {
        //amount of bytes received from client
        ssize_t bytes_received = recv(thread_func_args->client_socket_fd, recv_buffer, sizeof(recv_buffer), 0);

        if(bytes_received <= 0) //check for errors or closed connection
        {
            syslog(LOG_ERR, "recv() failed or connection closed");
            break;
        }

        /* temp variable to save increased size of data based on 
        prior data size and new bytes received */
        char *tmp = realloc(data, data_len + (size_t)bytes_received);

        if(tmp == NULL)
        {
            break;
        }

        data = tmp;

        //append new bytes to data
        memcpy(data + data_len, recv_buffer, (size_t)bytes_received);

        data_len += (size_t)bytes_received;

        /* how many bytes have traversed "data" until packet break found */
        size_t traversed = 0; 
        char *curr_position = 0; //current position in "data"

        /* loop until the end of the received data has been reached
        or until there's no complete packets */
        while(traversed < data_len) 
        {
            curr_position = data + traversed;
            //search for packet break
            char *end_of_packet = memchr(curr_position, '\n', data_len - traversed);

            //packet break not found
            if(end_of_packet == NULL) 
            {
                break;
            }
            
            size_t len_of_packet = (size_t)(end_of_packet - curr_position) + 1;

            //lock here so the worker threads can't overwrite each other
            if(pthread_mutex_lock(thread_func_args->mutex) != 0)
            {
                syslog(LOG_ERR,"mutex lock failed\n");
                cleanup_worker_thread_and_data(thread_func_args, data);
                return NULL;
            }

            if(packet_work(thread_func_args->fd, curr_position, len_of_packet, WRITE) != 0)
            {
                syslog(LOG_ERR, "write() failed");

                if(pthread_mutex_unlock(thread_func_args->mutex) != 0)
                {
                    syslog(LOG_ERR, "mutex unlock failed at write()\n");
                }
                cleanup_worker_thread_and_data(thread_func_args, data);
                return NULL;
            }

            //return to the beginning of the file
            off_t pos_in_file = lseek(thread_func_args->fd, 0, SEEK_SET);
            if(pos_in_file == (off_t) -1)
            {
                //error returning to the start of the file
                if(pthread_mutex_unlock(thread_func_args->mutex) != 0)
                {
                    syslog(LOG_ERR, "mutex unlock failed at lseek()\n");
                }
                cleanup_worker_thread_and_data(thread_func_args, data);
                return NULL;
            }

            while(true)
            {
                char read_buffer[KILOBYTE];

                ssize_t bytes_read = read(thread_func_args->fd, read_buffer, sizeof(read_buffer));

                if(bytes_read < 0) //check for errors or closed connection
                {
                    syslog(LOG_ERR, "read() failed or connection closed");
                    if(pthread_mutex_unlock(thread_func_args->mutex) != 0)
                    {
                        syslog(LOG_ERR, "mutex unlock failed at read()\n");
                    }
                    cleanup_worker_thread_and_data(thread_func_args, data);
                    return NULL;
                }
                else if(bytes_read == 0)
                {
                    break; //no more bytes to read from the file
                }
                if(packet_work(thread_func_args->client_socket_fd, read_buffer, bytes_read, SEND) != 0)
                {
                    syslog(LOG_ERR, "send() failed");
                    if(pthread_mutex_unlock(thread_func_args->mutex) != 0)
                    {
                        syslog(LOG_ERR, "mutex unlock failed at send()\n");
                    }
                    cleanup_worker_thread_and_data(thread_func_args, data);
                    return NULL;
                }
            }
            
            //done with this file work iteration so we can unlock
            if(pthread_mutex_unlock(thread_func_args->mutex) != 0)
            {
                syslog(LOG_ERR, "mutex unlock failed at end of recieve_send_client()\n");
            }

            traversed += len_of_packet;
            curr_position += len_of_packet;
        }

        if(traversed > 0)
        {
            //amount of data left to be worked
            size_t leftovers = data_len - traversed;

            if(leftovers > 0)
            {
                //moves the start of the new packet to the head of data
                memmove(data, curr_position, leftovers);
                data_len = leftovers;
            }
            //end of data
            else if(leftovers == 0)
            {
                free(data);
                data = NULL;
                data_len = 0;
            }
        }
    }
    
    cleanup_worker_thread_and_data(thread_func_args, data);
    return NULL;
}

int main(int argc, char **argv)
{
    int sockfd; //server socket
    int status;
    int option_value = 1; //value needed for setsockopt
    int client_sockfd; //client sockets

    struct addrinfo hints;
    struct addrinfo *servinfo;		//pointer to the result
    struct sockaddr_storage incoming_addr;
    struct sigaction new_action;
	pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    SLIST_HEAD(thread_data_list, thread_data) head;
    SLIST_INIT(&head);

    //make sure structs are empty
    memset(&new_action, 0, sizeof(new_action));
    memset(&hints, 0, sizeof(hints));
    
    //register signal handler for SIGINT and SIGTERM
    new_action.sa_handler = signal_handler;
    if(sigaction(SIGINT, &new_action, NULL) != 0)
    {
        syslog(LOG_ERR, "Error %d (%s) registering for SIGINT", errno, strerror(errno));
    }
    if(sigaction(SIGTERM, &new_action, NULL) != 0)
    {
        syslog(LOG_ERR, "Error %d (%s) registering for SIGTERM", errno, strerror(errno));
    }

    hints.ai_family = AF_UNSPEC; //IPv4 or IPv6 doesn't matter
    hints.ai_socktype = SOCK_STREAM; //TCP stream sockets
    hints.ai_flags = AI_PASSIVE; //fill IP for me

    if ((status = getaddrinfo(NULL, "9000", &hints, &servinfo)) != 0)
    {
        syslog(LOG_ERR, "getaddrinfo error: %s\n", gai_strerror(status));
        return -1;
    }

    sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if(sockfd == -1) //unable to find the server socket
    {
        syslog(LOG_ERR, "socket() ERROR");
        freeaddrinfo(servinfo);
        return -1;
    }

    //avoid bind() "Address Already In Use" error
    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &option_value, sizeof(option_value)) == -1)
    {
        cleanup_log_statement(LOG_ERR, "setsockopt() ERROR", sockfd, servinfo);
        return -1;
    }
    
    //check if we're able to bind to the server socket
    if(bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) == -1)
    {
        cleanup_log_statement(LOG_ERR, "bind() ERROR", sockfd, servinfo);
        return -1;
    }

    //enable daemon mode if requested
    if(argc == 2)
    { 
        //check to see if the argument was the daemon argument
        if(!strcmp(argv[1], "-d"))
        {
            //check to see if the daemon was successfully created
            if(daemon(0,0) == -1)
            {
                cleanup_log_statement(LOG_ERR, "daemon() ERROR", sockfd, servinfo);
                return -1;
            }
        }
    }

    //timer implementation
    //need to create timer after daemon so child has it as well
    timer_t timer;
    struct thread_data *timer_thread = malloc(sizeof *timer_thread);
    if(timer_thread != NULL)
    {
        int clock_id = CLOCK_MONOTONIC;
        struct sigevent sev;
        struct itimerspec timer_struct;
        
        memset(&sev, 0, sizeof(sev));
        memset(&timer_struct, 0, sizeof(timer_struct));

        timer_thread->mutex = &mutex;

        //register signal handling for SIGEV_THREAD
        sev.sigev_notify = SIGEV_THREAD;
        sev.sigev_value.sival_ptr = timer_thread;
        sev.sigev_notify_function = timer_function;
        timer_struct.it_value.tv_sec = INTITAL_ALARM; //first alarm after 10 seconds
        timer_struct.it_interval.tv_sec = INTERVAL; //subsequent alarms every 10 seconds
        if(timer_create(clock_id, &sev, &timer) != 0)
        {
            syslog(LOG_ERR, "Error %d (%s) creating timer", errno, strerror(errno));
        }

        if(timer_settime(timer, 0, &timer_struct, NULL) != 0)
        {
            syslog(LOG_ERR, "Error %d (%s) setting timer", errno, strerror(errno));
        }
    }
    else
    {
        syslog(LOG_ERR, "Malloc failed to allocate timer_thread\n");
    }

    //check if we're able to start listening for client sockets
    if(listen(sockfd, BACKLOG) == -1)
    {
        cleanup_log_statement(LOG_ERR, "listen ERROR", sockfd, servinfo);
        return -1;
    }

    int fd = -1;
    struct thread_data *td = NULL;
    while(!graceful_exit)
    {
        socklen_t addr_len = sizeof(incoming_addr);
        client_sockfd = accept(sockfd, (struct sockaddr*)&incoming_addr, &addr_len);

        //check to see if cliednt could connect
        if(client_sockfd != -1)
        {
            td = malloc(sizeof *td);
            if(td == NULL)
            {
                syslog(LOG_ERR, "Malloc failed to allocate struct\n");
                close(client_sockfd);
            }
            else
            {
                td->mutex = &mutex;
                td->thread_finished = false;
                char ip_buf[NI_MAXHOST]; //ip address buffer
                strncpy(td->client_ip, "XXXX", NI_MAXHOST);
                if(getnameinfo((struct sockaddr*)&incoming_addr, addr_len, ip_buf, sizeof(ip_buf), NULL, 0, NI_NUMERICHOST))
                {
                    syslog(LOG_ERR, "IP address could not be resolved");
                }
                else
                {   
                    strncpy(td->client_ip, ip_buf, NI_MAXHOST);
                    syslog(LOG_DEBUG, "Accepted connection from %s", td->client_ip);
                }

                fd = open(FILE_ADDR, O_RDWR | O_CREAT | O_APPEND, 0664);
                if (fd != -1)
                {
                    td->fd = fd;
                    td->client_socket_fd = client_sockfd;
                    SLIST_INSERT_HEAD(&head, td, entries);

                    if(pthread_create(&td->thread, NULL, receive_send_client, td) != 0)
                    {
                        perror("pthread_create error\n");
                        SLIST_REMOVE_HEAD(&head, entries);
                        if(close(client_sockfd) != -1)
                        {
                            syslog(LOG_DEBUG, "Closed connection from %s", td->client_ip);
                        }
                        free(td);
                        close(fd);
                    }
                }
                else
                {
                    syslog(LOG_ERR, "Failed to open file");
                    free(td);
                    close(client_sockfd);
                }
            }
        }
        else
        {
            syslog(LOG_ERR, "unable to connect to client");
        }
        
        struct thread_data *next;

        //removing threads that have completed from the linked list
        SLIST_FOREACH_SAFE(td, &head, entries, next)
        {
            if(td->thread_finished == true)
            {
                pthread_join(td->thread, NULL);
                SLIST_REMOVE(&head, td, thread_data, entries);
                free(td);
            }
        }
    }

    if (graceful_exit)
    {
        cleanup_log_statement(LOG_DEBUG, "Caught signal, exiting", sockfd, servinfo);
        closelog();

        struct thread_data *next;

        //making sure threads that havent finished yet get joined
        SLIST_FOREACH_SAFE(td, &head, entries, next)
        {
            pthread_join(td->thread, NULL);
            SLIST_REMOVE(&head, td, thread_data, entries);
            free(td);
        }
    }
    
    //close file if program ended early due to error or signal
    if(fd != -1)
    {
        close(fd);
    }

    //delete file
    remove(FILE_ADDR);
    timer_delete(timer);
    if(timer_thread != NULL)
    {
        free(timer_thread);
    }

    return 0;
}