#include "userprog/fdtable.h"

#include "filesys/file.h"
#include "threads/synch.h"
#include "threads/thread.h"

#include <debug.h>

/* Global filesystem lock. PintOS filesystem is not thread-safe,
   so all filesystem operations must be serialized with this lock. */
struct lock filesys_lock;

/* Initialize the global filesystem lock (called at system startup). */
void fd_table_init_lock(void) { lock_init(&filesys_lock); }

/* Allocate a file descriptor for the given file in the current thread's table.
   Returns the fd number (>= 2) on success, -1 if table is full.
   FD 0 and 1 are reserved for stdin and stdout. */
int fd_table_alloc(struct file *file)
{
  struct thread *cur = thread_current();
  int fd;

  ASSERT(file != NULL);

  /* FD 0 and 1 are reserved for stdin and stdout. */
  for (fd = 2; fd < FD_TABLE_SIZE; fd++)
  {
    if (cur->fd_table[fd] == NULL)
    {
      cur->fd_table[fd] = file;
      return fd;
    }
  }
  return -1; /* Table full. */
}

/* Get the file pointer for the given fd in the current thread's table.
   Returns NULL if fd is invalid or not open. */
struct file *fd_table_get(int fd)
{
  struct thread *cur = thread_current();

  if (fd < 0 || fd >= FD_TABLE_SIZE)
  {
    return NULL;
  }

  return cur->fd_table[fd];
}

/* Free (clear) the file descriptor slot without closing the file.
   The file must be closed separately if needed.
   Used when a syscall closes a file. */
void fd_table_free(int fd)
{
  struct thread *cur = thread_current();

  if (fd >= 0 && fd < FD_TABLE_SIZE)
  {
    cur->fd_table[fd] = NULL;
  }
}

/* Close all open file descriptors for the current thread.
   Called during process exit to clean up resources.
   FD 0 and 1 (stdin/stdout) are not closeable by the kernel. */
void fd_table_close_all(void)
{
  struct thread *cur = thread_current();
  int fd;

  for (fd = 2; fd < FD_TABLE_SIZE; fd++)
  {
    if (cur->fd_table[fd] != NULL)
    {
      lock_acquire(&filesys_lock);
      file_close(cur->fd_table[fd]);
      lock_release(&filesys_lock);
      cur->fd_table[fd] = NULL;
    }
  }
}
