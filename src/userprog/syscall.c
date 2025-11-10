#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <console.h> /* putbuf for console writes */
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "devices/shutdown.h" /* halt() */

static void syscall_handler (struct intr_frame *f) ;

static void syscall_halt(void);
static void syscall_exit(int status);
static pid_t syscall_exec(const char *file);
static int syscall_wait(pid_t pid);
static bool syscall_create(const char *file, unsigned initial_size);
static bool syscall_remove(const char *file);
static int syscall_open(const char *file);
static int syscall_filesize(int fd);
static int syscall_read(int fd, void *buffer, unsigned length);
static int syscall_write(int fd, const void *buffer, unsigned length);
static void syscall_seek(int fd, unsigned position);
static unsigned syscall_tell(int fd);
static void syscall_close(int fd);
static mapid_t syscall_mmap(int fd, void *addr);
static void syscall_munmap(mapid_t mapping);
static bool syscall_chdir(const char *dir);
static bool syscall_mkdir(const char *dir);
static bool syscall_readdir(int fd, char name[READDIR_MAX_LEN + 1]);
static bool syscall_isdir(int fd);
static int syscall_inumber(int fd);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Syscall flow:
 *
 * 1) User library (lib/user/syscall.c) pushes to the user stack syscall args, 
 *    then the syscall number, and executes interrupt `int $0x30`.
 * 2) On the trap, the CPU pushes the hardware frame; intr-stubs.S saves
 *    registers and user stack pointers, constructs a `struct intr_frame` on 
 *    the kernel stack and calls this ISR with `struct intr_frame *f`.
 * 3) `syscall_handler` reads the syscall number and arguments from the
 *    user stack pointer `f->esp` (the user stack at the moment of the trap),
 *    dispatches the syscall, and writes the return value into `f->eax`.
 * 4) When the handler returns, intr-stubs restores registers from the
 *    intr_frame and returns to user mode.
 */
static void
syscall_handler (struct intr_frame *f) 
{
  if (!f->esp) {
    thread_exit();
  }

  int syscall_num = *(int *)f->esp;

  switch (syscall_num) {
    case SYS_HALT:
      syscall_halt();
      break;

    case SYS_EXIT: {
      int status = ((int *)f->esp)[1];
      syscall_exit(status);
      break;
    }

    case SYS_EXEC: {
      const char *file = ((const char **)f->esp)[1];
      pid_t pid = syscall_exec(file);
      f->eax = pid;
      break;
    }

    case SYS_WAIT: {
      pid_t pid = ((pid_t *)f->esp)[1];
      int result = syscall_wait(pid);
      f->eax = result;
      break;
    }

    case SYS_CREATE: {
      const char *file = ((const char **)f->esp)[1];
      unsigned initial_size = ((unsigned *)f->esp)[2];
      bool ok = syscall_create(file, initial_size);
      f->eax = ok;
      break;
    }

    case SYS_REMOVE: {
      const char *file = ((const char **)f->esp)[1];
      bool ok = syscall_remove(file);
      f->eax = ok;
      break;
    }

    case SYS_OPEN: {
      const char *file = ((const char **)f->esp)[1];
      int fd = syscall_open(file);
      f->eax = fd;
      break;
    }

    case SYS_FILESIZE: {
      int fd = ((int *)f->esp)[1];
      int size = syscall_filesize(fd);
      f->eax = size;
      break;
    }

    case SYS_READ: {
      int fd = ((int *)f->esp)[1];
      void *buffer = ((void **)f->esp)[2];
      unsigned length = ((unsigned *)f->esp)[3];
      int read_bytes = syscall_read(fd, buffer, length);
      f->eax = read_bytes;
      break;
    }

    case SYS_WRITE: {
      int fd = ((int *)f->esp)[1];
      const void *buffer = ((const void **)f->esp)[2];
      unsigned length = ((unsigned *)f->esp)[3];
      int written = syscall_write(fd, buffer, length);
      f->eax = written;
      break;
    }

    case SYS_SEEK: {
      int fd = ((int *)f->esp)[1];
      unsigned position = ((unsigned *)f->esp)[2];
      syscall_seek(fd, position);
      break;
    }

    case SYS_TELL: {
      int fd = ((int *)f->esp)[1];
      unsigned pos = syscall_tell(fd);
      f->eax = pos;
      break;
    }

    case SYS_CLOSE: {
      int fd = ((int *)f->esp)[1];
      syscall_close(fd);
      break;
    }

    case SYS_MMAP: {
      int fd = ((int *)f->esp)[1];
      void *addr = ((void **)f->esp)[2];
      mapid_t mid = syscall_mmap(fd, addr);
      f->eax = mid;
      break;
    }

    case SYS_MUNMAP: {
      mapid_t mid = ((mapid_t *)f->esp)[1];
      syscall_munmap(mid);
      break;
    }

    case SYS_CHDIR: {
      const char *dir = ((const char **)f->esp)[1];
      bool ok = syscall_chdir(dir);
      f->eax = ok;
      break;
    }

    case SYS_MKDIR: {
      const char *dir = ((const char **)f->esp)[1];
      bool ok = syscall_mkdir(dir);
      f->eax = ok;
      break;
    }

    case SYS_READDIR: {
      int fd = ((int *)f->esp)[1];
      char name[READDIR_MAX_LEN + 1];
      bool ok = syscall_readdir(fd, name);
      f->eax = ok;
      break;
    }

    case SYS_ISDIR: {
      int fd = ((int *)f->esp)[1];
      bool dir = syscall_isdir(fd);
      f->eax = dir;
      break;
    }

    case SYS_INUMBER: {
      int fd = ((int *)f->esp)[1];
      int inum = syscall_inumber(fd);
      f->eax = inum;
      break;
    }

    default:
      thread_exit();
      break;
  }
}


static void
syscall_halt(void)
{
    shutdown_power_off();
}

static void
syscall_exit(int status)
{
    printf("%s: exit(%d)\n", thread_current()->name, status);
    thread_exit(); // TODO: Pass status
}

static pid_t
syscall_exec(const char *file)
{
    printf("syscall_exec not yet implemented\n");
    return (pid_t)-1;
}

static int
syscall_wait(pid_t pid)
{
    printf("syscall_wait not yet implemented\n");
    return -1;
}

static bool
syscall_create(const char *file, unsigned initial_size)
{
    printf("syscall_create not yet implemented\n");
    return false;
}

static bool
syscall_remove(const char *file)
{
    printf("syscall_remove not yet implemented\n");
    return false;
}

static int
syscall_open(const char *file)
{
    printf("syscall_open not yet implemented\n");
    return -1;
}

static int
syscall_filesize(int fd)
{
    printf("syscall_filesize not yet implemented\n");
    return -1;
}

static int
syscall_read(int fd, void *buffer, unsigned length)
{
    printf("syscall_read not yet implemented\n");
    return -1;
}

static int
syscall_write(int fd, const void *buffer, unsigned length)
{
    if (fd == 1) {
        putbuf((const char *)buffer, length);
        return (int) length;
    }
    printf("syscall_write: fd %d not yet implemented\n", fd);
    return -1;
}

static void
syscall_seek(int fd, unsigned position)
{
    printf("syscall_seek not yet implemented\n");
}

static unsigned
syscall_tell(int fd)
{
    printf("syscall_tell not yet implemented\n");
    return (unsigned)-1;
}

static void
syscall_close(int fd)
{
    printf("syscall_close not yet implemented\n");
}

static mapid_t
syscall_mmap(int fd, void *addr)
{
    printf("syscall_mmap not yet implemented\n");
    return (mapid_t)-1;
}

static void
syscall_munmap(mapid_t mapping)
{
    printf("syscall_munmap not yet implemented\n");
}

static bool
syscall_chdir(const char *dir)
{
    printf("syscall_chdir not yet implemented\n");
    return false;
}

static bool
syscall_mkdir(const char *dir)
{
    printf("syscall_mkdir not yet implemented\n");
    return false;
}

static bool
syscall_readdir(int fd, char name[READDIR_MAX_LEN + 1])
{
    printf("syscall_readdir not yet implemented\n");
    return false;
}

static bool
syscall_isdir(int fd)
{
    printf("syscall_isdir not yet implemented\n");
    return false;
}

static int
syscall_inumber(int fd)
{
    printf("syscall_inumber not yet implemented\n");
    return -1;
}
