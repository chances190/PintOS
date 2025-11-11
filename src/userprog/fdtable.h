#ifndef USERPROG_FDTABLE_H
#define USERPROG_FDTABLE_H

#include "filesys/file.h"
#include "threads/synch.h"

#include <stdbool.h>

/* File descriptor table management for user processes.
   Similar to pagedir module - provides a focused abstraction for
   managing the per-process file descriptor table. */

/* Global filesystem lock - serialize all filesystem operations. */
extern struct lock filesys_lock;

void fd_table_init_lock(void); /* Initialize global filesystem lock */
void fd_table_close_all(void); /* Close all fds in current process */
int fd_table_alloc(struct file *file);
struct file *fd_table_get(int fd);
void fd_table_free(int fd);

#endif /* userprog/fdtable.h */
