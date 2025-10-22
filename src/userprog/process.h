#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

#define ARG_MAX 64
#define EXEC_STR_MAX 512

tid_t process_execute (const char *exec_string);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
