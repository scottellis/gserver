# Makefile for gserver

# CC = gcc
CFLAGS = -Wall -O2

OBJS = main.o \
       commands.o \
       utility.o \

TARGET = gserver

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<


.PHONY: clean
clean:
	rm -f $(OBJS) $(TARGET)


