//Juan Barragan
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>


int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        printf("ERROR: Invalid Number of Arguments.\n");
	printf("Total number of arguments should be 2.\n");
	printf("The order of the arguments should be:\n");
	printf("    1)File Path.\n");
	printf("    2)String to be written in the specified file path.\n");
	return 1;
    }
	
    char *filePath = argv[1];
    char *word = argv[2];
    const char *newLine = "\n";
    int fd;
    ssize_t nr;
    ssize_t count = strlen(word);

    fd = open(filePath, O_WRONLY | O_CREAT | O_APPEND, 0664);
    if (fd == -1)
    {
        syslog(LOG_ERR, "Error opening file\n");
	return 1;
    }
    else
    {
	syslog(LOG_DEBUG, "File opened\n");
    }

    nr = write(fd, word, count);
    if (nr == -1)
    {
	syslog(LOG_ERR, "Error writing to file\n");
	close(fd);
	return 1;
    }
    else if (nr != count)
    {
    	syslog(LOG_DEBUG, "Possible Error\n");
    }
    else
    {
	write(fd, newLine, 1);
	syslog(LOG_DEBUG, "Writing %s to %s\n", word, filePath);
	return 0;
    }
}
