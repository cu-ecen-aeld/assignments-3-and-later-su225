#include <stdlib.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <pthread.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <arpa/inet.h>

#include "connhandler.h"
#include "linkedlist.h"
#include "linebuffer.h"

#define MAX_DATABUFFER_SIZE     1024
#define TIMESTAMP_INTERVAL_SECS 10

static linked_list_t conn_handlers;

static pthread_t timestamp_logger_thread;

static pthread_mutex_t outfile_lock;

static int outfilefd;
static int sockfd;

static atomic_bool close_conn_handler;

static void __conn_handler_server();
static void _conn_handler_free_handler_data(linked_list_node_t *node);
static void _conn_handler_subsystem_init_outfile();
static void _conn_handler_subsystem_start_timestamp_logger();
static void _conn_handler_close_sockets(linked_list_node_t *node);
static void *_conn_handler_do(void *a);

static int __conn_handler_write_to_outfile_threadsafe(char *line, size_t line_len);
static void *__conn_handler_timestamp_logger(void *a);

static void get_peer_address(struct sockaddr *addr, char *addr_buffer, int maxlen);

void conn_handler_subsystem_init() {
  atomic_store(&close_conn_handler, false);
  conn_handlers = linked_list_create();
  _conn_handler_subsystem_init_outfile();
  _conn_handler_subsystem_start_timestamp_logger();
  __conn_handler_server(); 
}

void conn_handler_subsystem_shutdown() {
  atomic_store(&close_conn_handler, true);

  close(sockfd);
  linked_list_destroy(&conn_handlers, _conn_handler_close_sockets);

  // Hold the lock while closing the outfile so that there are no
  // concurrent I/Os while this happens.
  pthread_mutex_lock(&outfile_lock);
  close(outfilefd);
  if (unlink(AESD_DATAFILE_PATH) < 0) {
    perror("failed to delete the datafile");
  }
  pthread_mutex_unlock(&outfile_lock);
  pthread_cancel(timestamp_logger_thread);
  pthread_join(timestamp_logger_thread, NULL);
}

static void __conn_handler_server() {
  struct addrinfo hints;
  struct addrinfo *servinfo;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  int status = getaddrinfo(NULL, "9000", &hints, &servinfo);
  if (status != 0) {
    fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
    exit(EXIT_FAILURE);
  }

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd == -1) {
    perror("cannot create server socket");
    freeaddrinfo(servinfo);
    exit(EXIT_FAILURE);
  }

  if (bind(sockfd, servinfo->ai_addr, sizeof(*(servinfo->ai_addr))) != 0) {
    perror("cannot bind the socket to port 9000");
    freeaddrinfo(servinfo);
    exit(EXIT_FAILURE);
  }
  freeaddrinfo(servinfo);
  
  if (listen(sockfd, SOMAXCONN) == -1) {
    perror("error listening on the socket");
    exit(EXIT_FAILURE);
  }

  while(!atomic_load(&close_conn_handler)) {
    struct sockaddr client_address;
    socklen_t client_address_len = sizeof(client_address);
    char client_addr_buffer[MAX_IP_LENGTH+1];

    int clientsockfd = accept(sockfd, &client_address, &client_address_len);
    if (clientsockfd < 0) {
      perror("error while accept()");
      break;
    }
    get_peer_address(&client_address, client_addr_buffer, MAX_IP_LENGTH+1);
    syslog(LOG_INFO, "Accepted connection from %s", client_addr_buffer);
    
    conn_handler_create_and_launch_handler(clientsockfd, client_addr_buffer);
  }
}

static void _conn_handler_close_sockets(linked_list_node_t *node) {
  conn_handler_t *h = (conn_handler_t *)(node->data);
  if (shutdown(h->clientsockfd, SHUT_RDWR) != 0) {
    perror("failed to shutdown client socket");
  }
  close(h->clientsockfd);
  // pthread_join(h->handler_thread, NULL);
  _conn_handler_free_handler_data(node);
}

static void _conn_handler_subsystem_init_outfile() {
  pthread_mutex_init(&outfile_lock, NULL);
  outfilefd = open(AESD_DATAFILE_PATH, O_CREAT | O_TRUNC | O_RDWR | O_APPEND, 0666);
  if (outfilefd == -1) {
    perror("error while opening the output file");
    exit(EXIT_FAILURE);
  }
}

static void _conn_handler_subsystem_start_timestamp_logger() {
  pthread_create(&timestamp_logger_thread, NULL, __conn_handler_timestamp_logger, NULL);
}

static void *__conn_handler_timestamp_logger(void *a) {
  const unsigned int TIME_BUFFER_SIZE = 256;
  char timestamp_buffer[TIME_BUFFER_SIZE];
  memset(timestamp_buffer, 0, TIME_BUFFER_SIZE);
  
  while (!atomic_load(&close_conn_handler)) {
    unsigned int rem_time = sleep(TIMESTAMP_INTERVAL_SECS);
    if (rem_time > 0 && atomic_load(&close_conn_handler)) {
      return NULL;
    }

    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) < 0) {
      perror("failed to disable timestamp logger cancelation");
      exit(EXIT_FAILURE);
    }
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    strftime(timestamp_buffer, TIME_BUFFER_SIZE, "timestamp:%a, %d %b %Y %T %z\n", tm_info);
    
    size_t ts_len = strlen(timestamp_buffer);
    int bytes_written = __conn_handler_write_to_outfile_threadsafe(timestamp_buffer, ts_len);
    if (bytes_written < 0) {
      perror("error while appending timestamp");
      goto reenable_thread_cancelation;
    }
    if (bytes_written < ts_len) {
      syslog(LOG_WARNING, "fewer bytes written than timestamp. Maybe running out of disk space");
    }
  
  reenable_thread_cancelation:
    if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) < 0) {
      perror("failed to re-enable timestamp logger cancelation");
      exit(EXIT_FAILURE);
    }
  }
  return NULL;
}

conn_handler_t *conn_handler_create_and_launch_handler(int clientsockfd, char *client_address) {
  conn_handler_t *h = (conn_handler_t*)malloc(sizeof(conn_handler_t));
  if (h == NULL) {
    perror("failed to allocate handler");
    return NULL;
  }
  size_t client_address_length = strlen(client_address);
  char *client_address_buffer = (char *)malloc(sizeof(char) * (client_address_length+1));
  if (client_address_buffer == NULL) {
    perror("failed to allocate for copying client address");
    return NULL;
  }
  strcpy(client_address_buffer, client_address);
  
  h->clientsockfd = clientsockfd;
  h->client_address = client_address_buffer;
  h->node = linked_list_append_front(&conn_handlers, (void *)h);
  if (h->node == NULL) {
    perror("failed to append to handlers list");
    return NULL;
  }
  pthread_create(&h->handler_thread, NULL, _conn_handler_do, h);
  return h;
}

static void *_conn_handler_do(void *a) {
  conn_handler_t *h = (conn_handler_t *)a;

  struct line_buffer lb;
  ssize_t line_len;
  ssize_t total_bytes_written = 0;
  char data_buffer[MAX_DATABUFFER_SIZE];

  line_buffer_init(&lb);

  while (!atomic_load(&close_conn_handler)) {
    int bytes_read = read(h->clientsockfd, data_buffer, MAX_DATABUFFER_SIZE);
    if (bytes_read == 0) { 
      break;
    }
    if (bytes_read < 0) {
      perror("error while reading from the socket");
      break;
    }
    int start = 0;
    for (int i = 0; i < bytes_read; i++) {
      if (data_buffer[i] != '\n') {
        continue;
      }
      line_buffer_append(&lb, data_buffer+start, i-start+1);
      char *line = line_buffer_get(&lb, &line_len);
      int bytes_written = __conn_handler_write_to_outfile_threadsafe(line, line_len);
      if (bytes_written < 0) {
        perror("error while appending line");
        goto cleanup_client; 
      }
      if (bytes_written < line_len) {
        syslog(LOG_WARNING, "writing entire bytes failed as the system is running out of disk space");
      }
      total_bytes_written += bytes_written;
      line_buffer_clear(&lb);

      off_t fileoffset = 0;
      while (true) {
        int res = sendfile(h->clientsockfd, outfilefd, &fileoffset, MAX_DATABUFFER_SIZE);
        if (res < 0) {
          perror("error while sending file output to socket");
          goto cleanup_client;
        }
        printf("sent %d bytes", res);
        if (res == 0) {
          break;
        }
      }

      start = i+1;
    }
    line_buffer_append(&lb, data_buffer+start, bytes_read-start);
  }
cleanup_client:
  close(h->clientsockfd);
  line_buffer_destroy(&lb);
  syslog(LOG_INFO, "Closed connection from %s", h->client_address);
  linked_list_remove_node(&conn_handlers, h->node, _conn_handler_free_handler_data);
  return NULL;
}

static void _conn_handler_free_handler_data(linked_list_node_t *node) {
  if (node == NULL) {
    return;
  }
  conn_handler_t *h = (conn_handler_t *)(node->data);
  free(h->client_address);
  free(h);
}

static int __conn_handler_write_to_outfile_threadsafe(char *line, size_t line_len) {
  pthread_mutex_lock(&outfile_lock);
  int bytes_written = write(outfilefd, line, line_len);
  pthread_mutex_unlock(&outfile_lock);
  return bytes_written;
}

static void get_peer_address(struct sockaddr *sa, char *addr_buffer, int maxlen) {
  switch (sa->sa_family) {
    case AF_INET:
      inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr), addr_buffer, maxlen);   
      break;

    case AF_INET6:
      inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr), addr_buffer, maxlen);
      break;
  }
}