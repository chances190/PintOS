#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

#define ARG_MAX 64

/* Map region identifier. */
typedef int mapid_t;
#define MAP_FAILED ((mapid_t) -1)

/* Maximum characters in a filename written by readdir(). */
#define READDIR_MAX_LEN 14

/* Typical return values from main() and arguments to exit(). */
#define EXIT_SUCCESS 0          /* Successful execution. */
#define EXIT_FAILURE 1          /* Unsuccessful execution. */

pid_t process_execute(const char *exec_string);
int process_wait(pid_t child_pid);
void process_exit(int status);
void process_activate(void);

bool install_page(void *upage, void *kpage, bool writable);

#endif /* userprog/process.h */
