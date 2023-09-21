#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <syslog.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#include "linebuffer.h"

#define AESD_DATAFILE_PATH    "/var/tmp/aesdsocketdata"
#define AESD_SERVER_PORT      9000
#define AESD_MAX_EPOLL_EVENTS 10

#define MAX_IP_LENGTH         32
#define MAX_DATABUFFER_SIZE   1024

void get_peer_address(struct sockaddr *addr, char *addr_buffer, int maxlen);

int sockfd;
int clientsockfd;
int outfilefd;

void signal_handler(int signo) {
  if (signo != SIGTERM && signo != SIGINT) {
    return;
  }
  const char *msg = "Caught signal, exiting";
  write(STDOUT_FILENO, msg, strlen(msg));
  close(sockfd);
  close(clientsockfd);
  close(outfilefd);
  unlink(AESD_DATAFILE_PATH);
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
  hints.ai_family = AF_UNSPEC;
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

  outfilefd = open(AESD_DATAFILE_PATH, O_CREAT | O_TRUNC | O_RDWR | O_APPEND, 0666);
  if (outfilefd == -1) {
    perror("error while opening the output file");
    exit(EXIT_FAILURE);
  }

  openlog(NULL, 0, LOG_USER);

  struct sockaddr client_address;
  socklen_t       client_address_len = sizeof(client_address);
  char            client_addr_buffer[MAX_IP_LENGTH+1];
  char            data_buffer[MAX_DATABUFFER_SIZE];

  struct line_buffer lb;
  ssize_t            line_len;

  line_buffer_init(&lb);

  ssize_t total_bytes_written = 0;
  while(1) {
    // We consider 1 client at a time model (very primitive) because it is
    // given that the data is written to /var/tmp/aesdsocketdata.
    clientsockfd = accept(sockfd, &client_address, &client_address_len);
    get_peer_address(&client_address, client_addr_buffer, MAX_IP_LENGTH+1);
    syslog(LOG_INFO, "Accepted connection from %s", client_addr_buffer);

    while (1) {
      int bytes_read = read(clientsockfd, data_buffer, MAX_DATABUFFER_SIZE);
      // There are no more bytes to read.
      // We have reached the end-of-file.
      if (bytes_read == 0) { 
        break;
      }
      // We have run into some trouble while
      // reading from the socket.
      if (bytes_read < 0) {
        perror("error while reading from the socket");
        break;
      }
      
      // Read line-by-line and append each line to the file.
      // On reading the newline we have to send the entire
      // contents of the file to the socket.
      int start = 0;
      for (int i = 0; i < bytes_read; i++) {
        if (data_buffer[i] != '\n') {
          continue;
        }
        line_buffer_append(&lb, data_buffer+start, i-start+1);
        char *line = line_buffer_get(&lb, &line_len);
        int bytes_written = write(outfilefd, line, line_len);
        if (bytes_written < 0) {
          perror("error while appending line");
          goto cleanup_client; 
        }
        if (bytes_written < line_len) {
          syslog(LOG_WARNING, "writing entire bytes failed as the system is running out of disk space");
        }
        total_bytes_written += bytes_written;
        line_buffer_clear(&lb);

        // The spec says we have to immediately output the
        // contents of the entire file to the socket
        off_t fileoffset = 0;
        while (fileoffset < total_bytes_written) {
          int res = sendfile(clientsockfd, outfilefd, &fileoffset, total_bytes_written-fileoffset);
          if (res < 0) {
            perror("error while sending file output to socket");
            goto cleanup_client;
          }
        }

        start = i+1;
      }
      line_buffer_append(&lb, data_buffer+start, bytes_read-start);
    }
  cleanup_client:
    close(clientsockfd);
    line_buffer_clear(&lb);
    syslog(LOG_INFO, "Closed connection from %s", client_addr_buffer);
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