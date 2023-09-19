CC ?= gcc
CFLAGS ?= -Werror -Wall -std=gnu17
OBJS ?= aesdsocket.c
TARGET ?= aesdsocket

all:
	${CC} $(CFLAGS) $(INCLUDES) $(OBJS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f aesdsocket
