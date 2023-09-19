#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <syslog.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <arpa/inet.h>

#define AESD_DATAFILE_PATH    "/var/tmp/aesdsocketdata"
#define AESD_SERVER_PORT      9000
#define AESD_MAX_EPOLL_EVENTS 10

#define MAX_IP_LENGTH         32
#define MAX_DATABUFFER_SIZE   1024

void get_peer_address(struct sockaddr *addr, char *addr_buffer, int maxlen);

int main(void) {
  int             sockfd;
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

  // setup a nonblocking socket. This is because epoll and edge-triggered
  // variant requires nonblocking so that either we read the data in the
  // socket buffer in full or get EAGAIN instead of reading partially and
  // getting blocked even when there is some data in the buffer. This
  // example scenario is described in the epoll() manual.
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd == -1) {
    perror("cannot create server socket");
    exit(EXIT_FAILURE);
  }

  if (bind(sockfd, servinfo->ai_addr, sizeof(*(servinfo->ai_addr))) != 0) {
    perror("cannot bind the socket to port 9000");
    exit(EXIT_FAILURE);
  }
  
  if (listen(sockfd, SOMAXCONN) == -1) {
    perror("error listening on the socket");
    exit(EXIT_FAILURE);
  }

  int outfilefd = open(AESD_DATAFILE_PATH, O_RDWR | O_CREAT, 0666);
  if (outfilefd == -1) {
    perror("error while opening the output file");
    exit(EXIT_FAILURE);
  }

  openlog(NULL, 0, LOG_USER);

  int             clientsockfd;
  struct sockaddr client_address;
  socklen_t       client_address_len = sizeof(client_address);
  char            client_addr_buffer[MAX_IP_LENGTH+1];
  char            data_buffer[MAX_DATABUFFER_SIZE];

  while(1) {
    // We consider 1 client at a time model (very primitive) because it is
    // given that the data is written to /var/tmp/aesdsocketdata.
    clientsockfd = accept(sockfd, &client_address, &client_address_len);
    get_peer_address(&client_address, client_addr_buffer, MAX_IP_LENGTH+1);
    syslog(LOG_INFO, "Accepted connection from %s", client_addr_buffer);

    // Now we read into the buffer and write to the file until we encounter
    // a new line. Once we encounter a newline, we have to read the contents
    // of the entire file and write it back to the client socket.
    int bytes_read, bytes_written;
    int total_bytes_read = 0;
    bool write_error = false;
    while (1) {
      bytes_read = read(clientsockfd, data_buffer, MAX_DATABUFFER_SIZE);
      if (bytes_read <= 0) {
        break;
      }
      total_bytes_read += bytes_read;

      // Write the data to the file
      int bytes_to_write = bytes_read;
      bool found_newline = false;
      for (int i = 0; i < bytes_read; ++i) {
        if (data_buffer[i] == '\n') {
          bytes_to_write = i+1;
          found_newline = true;
          break;
        }
      }
      bytes_written = write(outfilefd, data_buffer, bytes_to_write);
      if (bytes_written < 0) {
        write_error = true;
        perror("error while writing bytes to the client");
        break;
      }
      if (found_newline) {
        break;
      }
    }
    if (bytes_read < 0) {
      perror("error while reading from the client socket");
    }
    if (write_error) {
      syslog(LOG_ERR, "Error while writing data from %s", client_addr_buffer);
    } else {
      // Now write the data back to clientsockfd
      off_t offset = 0;
      int bytes_to_pipe = total_bytes_read;
      while (bytes_to_pipe > 0) {
        ssize_t cur_piped = sendfile(clientsockfd, outfilefd, &offset, bytes_to_pipe);
        if (cur_piped == -1) {
          perror("error while echoing packet to the client");
          syslog(LOG_ERR, "Error while echoing data from %s", client_addr_buffer);
          break;
        }
        bytes_to_pipe -= cur_piped;
      }
    }
    close(clientsockfd);
    truncate(AESD_DATAFILE_PATH, 0);
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