/** 
 * This program is used for connecting to a client using sockets
 * and reading and sending data back and forth to the client.
 * 
 * @author Juan Barragan
 */

#include "aesdsocket.h"

bool caught_sig = false; //flag set by signal handler

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
        caught_sig = true;
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
 * Overall process that performs writing received data from a client
 * and sending data back to the client after the binding process
 * has succeeded.
 *
 * @param fd file descriptor of the file to be worked
 * @param clientSockfd socket of the client we're connected to
 *
 */

static void receive_send_client(int fd, int clientSockfd)
{
    char *data = NULL; //client data
    size_t data_len = 0; //length of client data
    char recv_buffer[KILOBYTE]; //buffer to hold received data

    while(true)
    {
        //amount of bytes received from client
        ssize_t bytes_received = recv(clientSockfd, recv_buffer, sizeof(recv_buffer), 0);

        if(bytes_received <= 0) //check for errors or closed connection
        {
            syslog(LOG_ERR, "recv() failed or connection closed");
            free(data);
            return;
        }

        /* temp variable to save increased size of data based on 
        prior data size and new bytes received */
        char *tmp = realloc(data, data_len + (size_t)bytes_received);

        if(tmp == NULL)
        {
            free(data);
            return;
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

            if(packet_work(fd, curr_position, len_of_packet, WRITE) != 0)
            {
                syslog(LOG_ERR, "write() failed");
                break;
            }

            //return to the beginning of the file
            off_t pos_in_file = lseek(fd, 0, SEEK_SET);
            if(pos_in_file == (off_t) -1)
            {
                //error returning to the start of the file
                break;
            }

            while(true)
            {
                char read_buffer[KILOBYTE];
            
                ssize_t bytes_read = read(fd, read_buffer, sizeof(read_buffer));

                if(bytes_read < 0) //check for errors or closed connection
                {
                    syslog(LOG_ERR, "read() failed or connection closed");
                    break;
                }
                else if(bytes_read == 0)
                {
                    break; //no more bytes to read from the file
                }

                if(packet_work(clientSockfd, read_buffer, bytes_read, SEND) != 0)
                {
                    syslog(LOG_ERR, "send() failed");
                    break;
                }
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

    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &option_value, sizeof(option_value)) == -1)
    {
        cleanup_log_statement(LOG_ERR, "setsockopt ERROR", sockfd, servinfo);
        return -1;
    }
    
    //check if we're able to bind to the server socket
    if(bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) == -1)
    {
        cleanup_log_statement(LOG_ERR, "bind ERROR", sockfd, servinfo);
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
                cleanup_log_statement(LOG_ERR, "daemon ERROR", sockfd, servinfo);
                return -1;
            }
        }
    }

    //check if we're able to start listening for client sockets
    if(listen(sockfd, BACKLOG) == -1)
    {
        cleanup_log_statement(LOG_ERR, "listen ERROR", sockfd, servinfo);
        return -1;
    }

    int fd = -1;
    while(!caught_sig)
    {
        socklen_t addr_len = sizeof(incoming_addr);
        client_sockfd = accept(sockfd, (struct sockaddr*)&incoming_addr, &addr_len);

        //check to see if cliednt could connect
        if(client_sockfd != -1)
        {
            char ip_buf[NI_MAXHOST]; //ip address buffer
            const char *client_ip = "XXXX";
            if(getnameinfo((struct sockaddr*)&incoming_addr, addr_len, ip_buf, sizeof(ip_buf), NULL, 0, NI_NUMERICHOST))
            {
                syslog(LOG_ERR, "IP address could not be resolved");
            }
            else
            {   
                client_ip = ip_buf;
                syslog(LOG_DEBUG, "Accepted connection from %s", client_ip);
            }

            fd = open(FILE_ADDR, O_RDWR | O_CREAT | O_APPEND, 0664);

            if (fd != -1)
            {
                receive_send_client(fd, client_sockfd);
                close(fd);
                fd = -1;
            }
            else
            {
                syslog(LOG_ERR, "Failed to open file");
                close(client_sockfd);
            }

            if(close(client_sockfd) != -1)
            {
                syslog(LOG_DEBUG, "Closed connection from %s", client_ip);
            }

        }

        else
        {
            syslog(LOG_ERR, "unable to connect to client");
        }
    }

    if (caught_sig)
    {
        cleanup_log_statement(LOG_DEBUG, "Caught signal, exiting", sockfd, servinfo);
        closelog();
    }
    
    //close file if program ended early due to error or signal
    if(fd != -1)
    {
        close(fd);
    }

    //delete file
    remove(FILE_ADDR);

    return 0;
}