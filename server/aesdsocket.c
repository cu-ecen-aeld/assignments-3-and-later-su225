#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <syslog.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#include "connhandler.h"

#define AESD_SERVER_PORT 9000

void get_peer_address(struct sockaddr *addr, char *addr_buffer, int maxlen);

int sockfd;

void signal_handler(int signo) {
  if (signo != SIGTERM && signo != SIGINT) {
    return;
  }
  const char *msg = "Caught signal, exiting";
  write(STDOUT_FILENO, msg, strlen(msg));
  close(sockfd);
  closelog();
  _exit(EXIT_SUCCESS);
}

int main(int argc, char* argv[]) {
  bool daemon_mode = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-d") == 0) {
      daemon_mode = true;
      break;
    }
  }

  struct sigaction sa;
  sa.sa_flags = 0;
  sa.sa_handler = signal_handler;
  
  sigemptyset(&sa.sa_mask);
  sigaddset(&sa.sa_mask, SIGINT);
  sigaddset(&sa.sa_mask, SIGTERM);

  if (sigaction(SIGTERM, &sa, NULL) == -1) {
    perror("failed to register handler for SIGTERM");
    exit(EXIT_FAILURE);
  }

  if (sigaction(SIGINT, &sa, NULL) == -1) {
    perror("failed to register handler for SIGINT");
    exit(EXIT_FAILURE);
  }

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
    exit(EXIT_FAILURE);
  }

  if (bind(sockfd, servinfo->ai_addr, sizeof(*(servinfo->ai_addr))) != 0) {
    perror("cannot bind the socket to port 9000");
    exit(EXIT_FAILURE);
  }
  freeaddrinfo(servinfo);

  if (daemon_mode) {
    pid_t childpid = fork();
    if (childpid < 0) {
      perror("failed to fork() a daemon process");
      exit(EXIT_FAILURE);
    }
    if (childpid != 0) {
      // We are inside the parent
      exit(EXIT_SUCCESS);
    } else {
      // We are inside the child and we have to
      // daemonise this process
      umask(0);
      if (setsid() < 0) {
        perror("daemonize: error on setsid()");
        exit(EXIT_FAILURE);
      }
      if (chdir("/") < 0) {
        perror("daemonize: error on chdir()");
        exit(EXIT_FAILURE);
      }
      close(STDIN_FILENO);
      close(STDOUT_FILENO);
      close(STDERR_FILENO);
    }
  }
  
  if (listen(sockfd, SOMAXCONN) == -1) {
    perror("error listening on the socket");
    exit(EXIT_FAILURE);
  }

  conn_handler_subsystem_init();
  openlog(NULL, 0, LOG_USER);
  while(1) {
    struct sockaddr client_address;
    socklen_t client_address_len = sizeof(client_address);
    char client_addr_buffer[MAX_IP_LENGTH+1];

    int clientsockfd = accept(sockfd, &client_address, &client_address_len);
    get_peer_address(&client_address, client_addr_buffer, MAX_IP_LENGTH+1);
    syslog(LOG_INFO, "Accepted connection from %s", client_addr_buffer);
    
    conn_handler_create_and_launch_handler(clientsockfd, client_addr_buffer);
  }

  return 0;
}

void get_peer_address(struct sockaddr *sa, char *addr_buffer, int maxlen) {
  switch (sa->sa_family) {
    case AF_INET:
      inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr), addr_buffer, maxlen);   
      break;

    case AF_INET6:
      inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr), addr_buffer, maxlen);
      break;
  }
}