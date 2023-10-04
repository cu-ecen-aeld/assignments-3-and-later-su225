CC ?= gcc
CFLAGS ?= -g -O0 -Werror -Wall -std=gnu17
OBJS ?= aesdsocket.c linebuffer.c linkedlist.c connhandler.c
TARGET ?= aesdsocket
LDFLAGS ?= -lrt

all:
	${CC} $(CFLAGS) $(INCLUDES) $(OBJS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f aesdsocket
