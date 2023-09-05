#include "systemcalls.h"

#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{

/*
 * TODO  add your code here
 *  Call the system() function with the command set in the cmd
 *   and return a boolean true if the system() call completed with success
 *   or false() if it returned a failure
*/
    int ret = system(cmd);

    // According to the manual
    // 1. Status -1 is returned when the child process could not be created
    //    or its status cannot be retrieved.
    // 2. If a shell could not be executed in the child process, then the return
    //    value is as if the child shell terminated by calling exit() with status 127
    //
    // The exit status can be examined with WIFEXITED and WEXITSTATUS macros. According
    // to [2], WIFEXITED returns true if the child process exited normally.
    //
    // Reference:
    // 1. https://man7.org/linux/man-pages/man3/system.3.html#RETURN_VALUE
    // 2. https://man7.org/linux/man-pages/man2/waitpid.2.html#DESCRIPTION
    if (ret == -1) {
        return false;
    }
    return WIFEXITED(ret);
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

int mysystem(char *command[]);

bool do_exec(int count, ...) {
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++) {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

/*
 * TODO:
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
 *
*/
    int ret = mysystem(command);
    va_end(args);
    if (ret == -1) {
        return false;
    }
    return WIFEXITED(ret);
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];


/*
 * TODO
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/

    va_end(args);

    return true;
}

int mysystem(char *command[]) {
    pid_t child_pid = fork();
    if (child_pid == -1) {
        // Forking the process failed.
        return -1;
    }
    if (child_pid == 0) {
        // We are in the child process. Execute
        // the given command here.
        int exec_res = execv(command[0], command);
        if (exec_res == -1) {
            exit(127);
        }
    } else {
        // We are in the parent process. Wait for the
        // child to finish, collect the status and return
        // the same to the caller.
        int child_status;
        waitpid(child_pid, &child_status, 0);
        if (WIFEXITED(child_status)) {
            return WEXITSTATUS(child_status);
        }
    }
    return 0;
}