CC ?= gcc
CFLAGS ?= -Werror -Wall
OBJS ?= writer.c
TARGET ?= writer

build:
	${CC} $(CFLAGS) $(INCLUDES) $(OBJS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f writer