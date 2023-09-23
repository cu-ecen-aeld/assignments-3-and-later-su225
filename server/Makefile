CC ?= gcc
CFLAGS ?= -g -O0 -Werror -Wall -std=gnu17
OBJS ?= aesdsocket.c linebuffer.c
TARGET ?= aesdsocket

all:
	${CC} $(CFLAGS) $(INCLUDES) $(OBJS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f aesdsocket
