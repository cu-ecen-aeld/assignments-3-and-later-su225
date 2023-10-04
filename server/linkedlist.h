#ifndef __AESDSOCKET_ASSIGNMENT_LINKEDLIST_H
#define __AESDSOCKET_ASSIGNMENT_LINKEDLIST_H

#include <pthread.h>

typedef struct linked_list_node {
  struct linked_list_node *next;
  struct linked_list_node *prev;
  void *data;
}linked_list_node_t;

typedef struct linked_list {
  pthread_rwlock_t mu;
  linked_list_node_t *head;
  linked_list_node_t *tail;
}linked_list_t;

linked_list_t linked_list_create();
linked_list_node_t* linked_list_append_front(linked_list_t *list, void *data);
linked_list_node_t* linked_list_append_back(linked_list_t *list, void *data);
void linked_list_foreach_node(linked_list_t *list, void (*f)(linked_list_node_t *node));
void linked_list_remove_node(
  linked_list_t *list,
  linked_list_node_t *to_remove,
  void (*node_data_cleanup_func)(linked_list_node_t *)
);

#endif