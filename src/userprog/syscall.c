#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <console.h> /* putbuf for console writes */
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "devices/shutdown.h" /* halt() */

static void syscall_handler (struct intr_frame *f);

static bool memcpy_from_user(void *kernel_dst, const void *user_src, size_t size);
static bool strncpy_from_user(char *kernel_dst, void *user_src, size_t max_len);
static bool memcpy_to_user(void *user_dst, const void *kernel_src, size_t size);
static bool strncpy_to_user(void *user_dst, const char *kernel_src, size_t max_len);

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
  // Validate the user stack pointer and read syscall number
  int syscall_num;
  if (!f->esp || !memcpy_from_user(&syscall_num, f->esp, sizeof(syscall_num))) {
    syscall_exit(-1);
  }

  switch (syscall_num) {
    case SYS_HALT:
      syscall_halt();
      break;

    case SYS_EXIT: {
      int status;
      if (!memcpy_from_user(&status, (int *)f->esp + 1, sizeof(status))) {
        syscall_exit(-1);
      }
      syscall_exit(status);
      break;
    }

    case SYS_EXEC: {
      const char *file;
      if (!memcpy_from_user(&file, (const char **)f->esp + 1, sizeof(file))) {
        syscall_exit(-1);
      }
      pid_t pid = syscall_exec(file);
      f->eax = pid;
      break;
    }

    case SYS_WAIT: {
      pid_t pid;
      if (!memcpy_from_user(&pid, (pid_t *)f->esp + 1, sizeof(pid))) {
        syscall_exit(-1);
      }
      int result = syscall_wait(pid);
      f->eax = result;
      break;
    }

    case SYS_CREATE: {
      const char *file;
      unsigned initial_size;
      if (!memcpy_from_user(&file, (const char **)f->esp + 1, sizeof(file)) ||
          !memcpy_from_user(&initial_size, (unsigned *)f->esp + 2, sizeof(initial_size))) {
        syscall_exit(-1);
      }
      bool ok = syscall_create(file, initial_size);
      f->eax = ok;
      break;
    }

    case SYS_REMOVE: {
      const char *file;
      if (!memcpy_from_user(&file, (const char **)f->esp + 1, sizeof(file))) {
        syscall_exit(-1);
      }
      bool ok = syscall_remove(file);
      f->eax = ok;
      break;
    }

    case SYS_OPEN: {
      const char *file;
      if (!memcpy_from_user(&file, (const char **)f->esp + 1, sizeof(file))) {
        syscall_exit(-1);
      }
      int fd = syscall_open(file);
      f->eax = fd;
      break;
    }

    case SYS_FILESIZE: {
      int fd;
      if (!memcpy_from_user(&fd, (int *)f->esp + 1, sizeof(fd))) {
        syscall_exit(-1);
      }
      int size = syscall_filesize(fd);
      f->eax = size;
      break;
    }

    case SYS_READ: {
      int fd;
      void *buffer;
      unsigned length;
      if (!memcpy_from_user(&fd, (int *)f->esp + 1, sizeof(fd)) ||
          !memcpy_from_user(&buffer, (void **)f->esp + 2, sizeof(buffer)) ||
          !memcpy_from_user(&length, (unsigned *)f->esp + 3, sizeof(length))) {
        syscall_exit(-1);
      }
      int read_bytes = syscall_read(fd, buffer, length);
      f->eax = read_bytes;
      break;
    }

    case SYS_WRITE: {
      int fd;
      const void *buffer;
      unsigned length;
      if (!memcpy_from_user(&fd, (int *)f->esp + 1, sizeof(fd)) ||
          !memcpy_from_user(&buffer, (const void **)f->esp + 2, sizeof(buffer)) ||
          !memcpy_from_user(&length, (unsigned *)f->esp + 3, sizeof(length))) {
        syscall_exit(-1);
      }
      int written = syscall_write(fd, buffer, length);
      f->eax = written;
      break;
    }

    case SYS_SEEK: {
      int fd;
      unsigned position;
      if (!memcpy_from_user(&fd, (int *)f->esp + 1, sizeof(fd)) ||
          !memcpy_from_user(&position, (unsigned *)f->esp + 2, sizeof(position))) {
        syscall_exit(-1);
      }
      syscall_seek(fd, position);
      break;
    }

    case SYS_TELL: {
      int fd;
      if (!memcpy_from_user(&fd, (int *)f->esp + 1, sizeof(fd))) {
        syscall_exit(-1);
      }
      unsigned pos = syscall_tell(fd);
      f->eax = pos;
      break;
    }

    case SYS_CLOSE: {
      int fd;
      if (!memcpy_from_user(&fd, (int *)f->esp + 1, sizeof(fd))) {
        syscall_exit(-1);
      }
      syscall_close(fd);
      break;
    }

    case SYS_MMAP: {
      int fd;
      void *addr;
      if (!memcpy_from_user(&fd, (int *)f->esp + 1, sizeof(fd)) ||
          !memcpy_from_user(&addr, (void **)f->esp + 2, sizeof(addr))) {
        syscall_exit(-1);
      }
      mapid_t mid = syscall_mmap(fd, addr);
      f->eax = mid;
      break;
    }

    case SYS_MUNMAP: {
      mapid_t mid;
      if (!memcpy_from_user(&mid, (mapid_t *)f->esp + 1, sizeof(mid))) {
        syscall_exit(-1);
      }
      syscall_munmap(mid);
      break;
    }

    case SYS_CHDIR: {
      const char *dir;
      if (!memcpy_from_user(&dir, (const char **)f->esp + 1, sizeof(dir))) {
        syscall_exit(-1);
      }
      bool ok = syscall_chdir(dir);
      f->eax = ok;
      break;
    }

    case SYS_MKDIR: {
      const char *dir;
      if (!memcpy_from_user(&dir, (const char **)f->esp + 1, sizeof(dir))) {
        syscall_exit(-1);
      }
      bool ok = syscall_mkdir(dir);
      f->eax = ok;
      break;
    }

    case SYS_READDIR: {
      int fd;
      char name[READDIR_MAX_LEN + 1];
      if (!memcpy_from_user(&fd, (int *)f->esp + 1, sizeof(fd))) {
        syscall_exit(-1);
      }
      bool ok = syscall_readdir(fd, name);
      f->eax = ok;
      break;
    }

    case SYS_ISDIR: {
      int fd;
      if (!memcpy_from_user(&fd, (int *)f->esp + 1, sizeof(fd))) {
        syscall_exit(-1);
      }
      bool dir = syscall_isdir(fd);
      f->eax = dir;
      break;
    }

    case SYS_INUMBER: {
      int fd;
      if (!memcpy_from_user(&fd, (int *)f->esp + 1, sizeof(fd))) {
        syscall_exit(-1);
      }
      int inum = syscall_inumber(fd);
      f->eax = inum;
      break;
    }

    default:
      thread_exit();
      break;
  }
}

/* Reads a byte from user virtual address UADDR to KERNEL_DST.
  UADDR must be below PHYS_BASE.
  Returns true if successful, false if a segfault occurred. */
static bool
_get_byte_from_user (uint8_t *kernel_dst, const uint8_t *user_src)
{
  int error_code;
  uint8_t value;
  
  asm ("movl $1f, %0; movzbl %2, %k1; 1:"
     : "=&a" (error_code), "=&r" (value)  // outputs
     : "m" (*user_src)                    // input: memory at user_src
     : "memory");                         // clobbers memory
  
  *kernel_dst = value;
  return error_code != -1;
}

/* Reads SIZE bytes from user virtual address USER_SRC into KERNEL_DST.
  Returns true if successful, false if a segfault occurred. */
static bool
memcpy_from_user (void *kernel_dst, const void *user_src, size_t size)
{
  uint8_t *dst = kernel_dst;
  const uint8_t *src = user_src;

  if (!is_user_vaddr(src) || !is_user_vaddr(src + size - 1)) 
  {
    return false;
  }

  for (size_t i = 0; i < size; i++) 
  {
   if (!_get_byte_from_user(dst++, src++)) 
   {
    return false;
   }
  }
  return true;
}

/* Reads a null-terminated string from user virtual address USER_SRC into KERNEL_DST.
  Returns true if successful, false if a segfault occurred or the string is too long. */
static bool
strncpy_from_user (char *kernel_dst, void *user_src, size_t max_len)
{
  size_t i;
  for (i = 0; i < max_len; i++) 
  {
   if (!is_user_vaddr(user_src) || !_get_byte_from_user((uint8_t *)&kernel_dst[i], (const uint8_t *)user_src)) 
   {
    return false;
   }
   if (kernel_dst[i] == '\0') 
   {
    return true;
   }
   user_src++;
  }
  // String exceeded max_len
  return false;
}

/* Writes BYTE to user address UADDR.
  UADDR must be below PHYS_BASE.
  Returns true if successful, false if a segfault occurred. */
static bool
_put_byte_to_user (uint8_t *user_dst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
     : "=&a" (error_code), "=m" (*user_dst) // output: memory at user_dst
     : "q" (byte)                           // input
     : "memory");
  return error_code != -1;
}

/* Writes SIZE bytes from KERNEL_SRC to user virtual address USER_DST.
  Returns true if successful, false if a segfault occurred. */
static bool
memcpy_to_user(void *user_dst, const void *kernel_src, size_t size)
{
  uint8_t *dst = user_dst;
  const uint8_t *src = kernel_src;

  if (!is_user_vaddr(dst) || !is_user_vaddr(dst + size - 1)) 
  {
    return false;
  }

  for (size_t i = 0; i < size; i++) 
  {
   if (!_put_byte_to_user(dst++, *src++)) 
   {
    return false;
   }
  }
  return true;
}

/* Writes a null-terminated string from KERNEL_SRC to user virtual address USER_DST.
  Returns true if successful, false if a segfault occurred or the string is too long. */
static bool
strncpy_to_user(void *user_dst, const char *kernel_src, size_t max_len)
{
  size_t i;
  for (i = 0; i < max_len; i++) 
  {
    if (!is_user_vaddr(user_dst) || !_put_byte_to_user(user_dst, kernel_src[i])) 
    {
      return false;
    }
    if (kernel_src[i] == '\0') 
    {
      return true;
    }
    user_dst = (uint8_t *)user_dst + 1;
  }
  // String exceeded max_len
  return false;
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
