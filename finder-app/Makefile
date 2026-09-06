# Makefile
CROSS_COMPILE ?=
CC := $(CROSS_COMPILE)gcc
CFLAGS := -Wall -Wextra -Werror
TARGET := writer
OBJECTS := writer.o

.PHONY: all clean

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) *.o

all: $(TARGET)
