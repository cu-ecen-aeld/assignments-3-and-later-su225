#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
// #define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    struct thread_data *param = (struct thread_data *)thread_param;

    if (nanosleep(&param->wait_to_obtain, NULL) != 0) {
        perror("error while waiting for mutex");
        return thread_param;
    }

    if (pthread_mutex_lock(param->mutex) != 0) {
        perror("error while acquiring mutex");
        return thread_param;
    }
    bool is_success = (nanosleep(&param->wait_to_release, NULL) == 0);
    if (pthread_mutex_unlock(param->mutex) != 0) {
        perror("error while releasing mutex");
        return thread_param;
    }
    
    param->thread_complete_success = is_success;
    return thread_param;
}

struct timespec to_timespec(long millis) {
    struct timespec t;
    t.tv_sec = millis / 1000;
    t.tv_nsec = (millis % 1000) * 1000000;
    return t;
}

bool start_thread_obtaining_mutex(
    pthread_t *thread, 
    pthread_mutex_t *mutex,
    int wait_to_obtain_ms, 
    int wait_to_release_ms
) {
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    struct thread_data *tdata = (struct thread_data*)malloc(sizeof(struct thread_data));
    if (tdata == NULL) {
        perror("failed to allocate memory for thread_data");
        return false;
    }
    tdata->mutex = mutex;
    tdata->wait_to_obtain = to_timespec((long)wait_to_obtain_ms);
    tdata->wait_to_release = to_timespec((long)wait_to_release_ms);
    tdata->thread_complete_success = false;

    if (pthread_create(thread, NULL, threadfunc, (void *)tdata) != 0) {
        perror("failed to create thread");
        return false;
    }

    return true;
}
