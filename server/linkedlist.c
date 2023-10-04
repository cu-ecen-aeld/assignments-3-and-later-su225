#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "linkedlist.h"

static linked_list_node_t *linked_list_node_create_with_data(void *data);

linked_list_t linked_list_create() {
  linked_list_t list = {
    .head = NULL,
    .tail = NULL,
    .mu = PTHREAD_RWLOCK_INITIALIZER,
  };
  return list;
}

linked_list_node_t* linked_list_append_front(linked_list_t *list, void *data) {
  if (list == NULL) {
    return NULL;
  }
  pthread_rwlock_wrlock(&list->mu);
  linked_list_node_t *new_node = linked_list_node_create_with_data(data);
  if (new_node == NULL) {
    return NULL;
  }
  if (list->head == NULL) {
    list->tail = new_node;
  } else {
    linked_list_node_t *prev_head = list->head;
    new_node->next = prev_head;
    prev_head->prev = new_node;
  }
  list->head = new_node;
  pthread_rwlock_unlock(&list->mu);
  return new_node;
}

linked_list_node_t* linked_list_append_back(linked_list_t *list, void *data) {
  if (list == NULL) {
    return NULL;
  }
  pthread_rwlock_wrlock(&list->mu);
  linked_list_node_t *new_node = linked_list_node_create_with_data(data);
  if (new_node == NULL) {
    return NULL;
  }
  if (list->head == NULL) {
    list->head = new_node;
  } else {
    linked_list_node_t *prev_tail = list->tail;
    new_node->prev = prev_tail;
    prev_tail->next = new_node;
  }
  list->tail = new_node;
  pthread_rwlock_unlock(&list->mu);
  return new_node;
}

void linked_list_foreach_node(linked_list_t *list, void (*f)(linked_list_node_t *node)) {
  if (list == NULL || list->head == NULL) {
    return;
  }
  pthread_rwlock_rdlock(&list->mu);
  linked_list_node_t *cur_node = list->head;
  while (cur_node != NULL) {
    f(cur_node);
    cur_node = cur_node->next;
  }
  pthread_rwlock_unlock(&list->mu);
}

void linked_list_remove_node(
  linked_list_t *list,
  linked_list_node_t *to_remove,
  void (*node_data_cleanup_func)(linked_list_node_t *)
) {
  if (list == NULL || to_remove == NULL) {
    return;
  }
  pthread_rwlock_wrlock(&list->mu);
  bool is_head_node = list->head == to_remove;
  bool is_tail_node = list->tail == to_remove;
  linked_list_node_t *next_node = to_remove->next;
  linked_list_node_t *prev_node = to_remove->prev;
  if (prev_node != NULL) {
    prev_node->next = next_node;
  }
  if (next_node != NULL) {
    next_node->prev = prev_node;
  }
  if (is_head_node) {
    list->head = next_node;
  }
  if (is_tail_node) {
    list->tail = prev_node;
  }
  pthread_rwlock_unlock(&list->mu);

  if (node_data_cleanup_func != NULL) {
    node_data_cleanup_func(to_remove);
  }
  free(to_remove);
}

static linked_list_node_t *linked_list_node_create_with_data(void *data) {
  linked_list_node_t *new_node = (linked_list_node_t*)malloc(sizeof(linked_list_node_t));
  if (new_node == NULL) {
    perror("cannot allocate new linked list node");
    return NULL;   
  }
  new_node->next = NULL;
  new_node->prev = NULL;
  new_node->data = data;
  return new_node;
}