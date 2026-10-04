# Makefile
CROSS_COMPILE ?=
CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Wextra -Werror
TARGET ?= aesdsocket
LDFLAGS ?= -lpthread -lrt
OBJECTS ?= aesdsocket.o

.PHONY: all clean

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET) *.o

all: $(TARGET)
