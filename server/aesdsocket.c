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

#include "connhandler.h"

static pthread_t sig_handler_thread;

static void *__signal_handler(void *data) {
  sigset_t *set = (sigset_t *)data;
  int s, sig;
  for (;;) {
    s = sigwait(set, &sig);
    if (s != 0) {
      perror("error while waiting for the signal");
      exit(EXIT_FAILURE);  
    }
    printf("got signal: %d", sig);
    syslog(LOG_INFO, "Caught signal, exiting");
    closelog();
    conn_handler_subsystem_shutdown();
    _exit(EXIT_SUCCESS);
  }
}

static void _launch_signal_handler_thread(sigset_t *sigmask) {
  pthread_create(&sig_handler_thread, NULL, __signal_handler, sigmask);
}

int main(int argc, char* argv[]) {
  bool daemon_mode = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-d") == 0) {
      daemon_mode = true;
      break;
    }
  }

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

  // Block the important signals on the main thread. According to
  // the pthread_sigmask spec required by POSIX, the sigmask is
  // inherited by the threads created from this thread and hence
  // is applicable to all the threads handling connection. However,
  // we also create a dedicated thread to handle the signals.
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGINT);
  sigaddset(&set, SIGTERM);
  if (pthread_sigmask(SIG_BLOCK, &set, NULL) < 0) {
    perror("error while masking out the signals in main thread");
    exit(EXIT_FAILURE);
  }

  openlog(NULL, 0, LOG_USER);
  _launch_signal_handler_thread(&set);
  conn_handler_subsystem_init();

  return 0;
}
