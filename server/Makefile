# Makefile
CROSS_COMPILE ?=
CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Wextra -Werror
USE_AESD_CHAR_DEVICE ?= 1
CPFLAGS += -DUSE_AESD_CHAR_DEVICE=$(USE_AESD_CHAR_DEVICE)
TARGET ?= aesdsocket
LDFLAGS ?= -lpthread -lrt
OBJECTS ?= aesdsocket.o

.PHONY: all clean

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@

%.o: %.c
	$(CC) $(CPFLAGS) $(CFLAGS) -c $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET) *.o

all: $(TARGET)
