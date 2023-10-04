#ifndef __AESDSOCKET_ASSIGNMENT_CONNHANDLER_H
#define __AESDSOCKET_ASSIGNMENT_CONNHANDLER_H

#include <pthread.h>

#include "linkedlist.h"

#define AESD_DATAFILE_PATH "/var/tmp/aesdsocketdata"
#define MAX_IP_LENGTH      32

typedef struct conn_handler {
  int clientsockfd;
  char *client_address;
  pthread_t handler_thread;
  linked_list_node_t *node;
}conn_handler_t;

void conn_handler_subsystem_init();

conn_handler_t *conn_handler_create_and_launch_handler(int clientsockfd, char *client_address);

#endif