#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/fdtable.h"
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static thread_func start_process NO_RETURN;
static bool load (const char *exec_string, void (**eip) (void), void **esp);
static struct process_exec_status* process_exec_status_init();

struct start_process_args 
{
  char *exec_string;
  struct process_exec_status *exec_status;
};

/* Starts a new thread running a user program loaded with
   EXEC_STRING. The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or PID_ERROR if the thread cannot be created. */
pid_t
process_execute (const char *exec_string) 
{
  char *exec_string_cp = NULL, *filename = NULL;
  size_t fn_len;
  tid_t tid;
  struct thread *cur = thread_current ();
  struct thread *child;
  struct process_exec_status *child_exec_status;

  /* Make a copy of FILE_NAME for the child process
  (first token before space) */
  filename = palloc_get_page (0);
  if (filename == NULL) return TID_ERROR;
  fn_len = strcspn(exec_string, " ");
  if (fn_len >= PGSIZE) fn_len = PGSIZE - 1;
  strlcpy(filename, exec_string, fn_len + 1);

  /* Make a copy of EXEC_STRING for the child process */
  exec_string_cp = palloc_get_page (0);
  if (exec_string_cp == NULL)
  {
    palloc_free_page(filename);
    return PID_ERROR;
  }
  strlcpy (exec_string_cp, exec_string, PGSIZE);

  /* Create an EXEC_STATUS struct for the child process. */
  DEBUG_PRINT("[process_execute] Creating child status\n");
  child_exec_status = process_exec_status_init();
  if (child_exec_status == NULL)
  {
    palloc_free_page(filename);
    palloc_free_page(exec_string_cp);
    return PID_ERROR;
  }
  
  /* Pack the arguments of start_process into a struct*/
  struct start_process_args *args = malloc(sizeof(struct start_process_args));
  if (args == NULL) {
    free(child_exec_status);
    palloc_free_page(filename);
    palloc_free_page(exec_string_cp);
    return PID_ERROR;
  }
  args->exec_string = exec_string_cp;
  args->exec_status = child_exec_status;
  
  /* Create a new thread to execute FILENAME as an user process. */
  DEBUG_PRINT("[process_execute] Creating thread for '%s'\n", filename);
  tid = thread_create(filename, PRI_DEFAULT, start_process, args);
  palloc_free_page(filename);
  if (tid == TID_ERROR)
  {
    /* Couldn't create thread for child process */
    DEBUG_PRINT("[process_execute] thread_create failed\n");
    free(child_exec_status);
    palloc_free_page(exec_string_cp);
    free(args);
    return PID_ERROR;
  }
  
  /* Add the new process as a child of the current process. */
  DEBUG_PRINT("[process_execute] Process created with pid=%d\n", tid);
  list_push_back(&cur->children, &child_exec_status->elem);
  
  /* Wait for child to finish loading. */
  DEBUG_PRINT("[process_execute] Waiting for child to load...\n");
  sema_down (&child_exec_status->wait_sema);

  if (child_exec_status->pid == PID_ERROR) 
  {
    /* Couldn't initialize child process. */
    DEBUG_PRINT("[process_execute] Load failed\n");
    return PID_ERROR;
  }

  DEBUG_PRINT("[process_execute] Load completed. Returning pid=%d\n", tid);
  return (pid_t) tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *args_)
{
  struct start_process_args *args = args_;
  char *exec_string = args->exec_string;
  struct process_exec_status *child_stat = args->exec_status;
  
  struct intr_frame if_;
  bool success;
  
  struct thread *cur = thread_current ();
  cur->exec_status = child_stat;
  cur->exec_status->pid = cur->tid;
  DEBUG_PRINT("[start_process] tid=%d starting, exec_string='%s'\n", cur->tid, exec_string);

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  
  DEBUG_PRINT("[start_process] Calling load()...\n");
  success = load (exec_string, &if_.eip, &if_.esp);
  DEBUG_PRINT("[start_process] load() returned %d\n", success);
  palloc_free_page (exec_string);
  free(args);
  
  if (!success)
  {
    /* Load failed - mark as exited with -1. */
    cur->exec_status->has_exited = true;
    cur->exec_status->exit_status = -1;
  }
  
  DEBUG_PRINT("[start_process] Signaling parent (sema_up on wait_sema)\n");
  sema_up (&cur->exec_status->wait_sema);
  
  if (!success)
  {
    DEBUG_PRINT("[start_process] Load failed, exiting with status -1\n");
    thread_exit ();
  }

  DEBUG_PRINT("[start_process] Jumping to user mode...\n");
  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (pid_t child_tid) 
{
  struct thread *cur = thread_current ();
  struct process_exec_status *child_stat = NULL;
  struct list_elem *e;
  int exit_status;
  bool should_wait;
  
  DEBUG_PRINT("[process_wait] tid=%d waiting for child_tid=%d\n", cur->tid, child_tid);
  
  /* Find the process_exec_status with the given TID. */
  for (e = list_begin (&cur->children); e != list_end (&cur->children);
       e = list_next (e))
  {
    struct process_exec_status *cs = list_entry (e, struct process_exec_status, elem);
    if (cs->pid == child_tid)
    {
      child_stat = cs;
      break;
    }
  }
  
  /* Return -1 if no such child. */
  if (child_stat == NULL)
  {
    DEBUG_PRINT("[process_wait] Child not found, returning -1\n");
    return -1;
  }
  
  /* Check if already waited on this child. */
  if (child_stat->waited_on)
  {
    DEBUG_PRINT("[process_wait] Already waited on this child, returning -1\n");
    return -1;
  }
  
  /* Mark as waited on and wait unconditionally. */
  DEBUG_PRINT("[process_wait] Waiting for child to exit (sema_down on wait_sema)...\n");
  child_stat->waited_on = true;
  sema_down (&child_stat->wait_sema);
  
  exit_status = child_stat->exit_status;  
  list_remove (&child_stat->elem);
  free(child_stat);
  
  DEBUG_PRINT("[process_wait] Child exited with status %d\n", exit_status);
  return exit_status;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  struct process_exec_status *my_status = cur->exec_status;
  ASSERT (my_status != NULL);

  DEBUG_PRINT("[process_exit] tid=%d exiting\n", cur->tid);

  /* Lock REQUIRED: both parent and child 
     access orphan/has_exited to decide who frees. */
  lock_acquire(&my_status->lock);
  my_status->has_exited = true;
  bool orphan = my_status->orphan;
  lock_release(&my_status->lock);
  
  DEBUG_PRINT("[process_exit] %s\n", orphan
              ? "Parent gone, will free process_exec_status"
              : "Signaling parent (sema_up on wait_sema)");
  
  if (orphan) free(my_status); /* Parent is gone. We own this structure now. */
  else sema_up(&my_status->wait_sema); /* Parent is waiting. Signal it. */


  while (!list_empty(&cur->children))
  {
    struct list_elem *e = list_pop_front(&cur->children);
    struct process_exec_status *cs = list_entry(e, struct process_exec_status, elem);
    
    /* Lock REQUIRED: both parent and child access 
       orphan/has_exited to decide who frees. */
    lock_acquire(&cs->lock);
    cs->orphan = true;
    bool should_free_child = cs->has_exited;
    lock_release(&cs->lock);
    
    DEBUG_PRINT("[process_exit] Child tid=%d %s\n", cs->pid, should_free_child
                ? "already exited, will free its process_exec_status"
                : "still running, marking as orphaned");
    
    if (should_free_child) free(cs);
  }

  /* Close all open file descriptors. */
  fd_table_close_all();

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (const char *exec_string, void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable invoked with EXEC_STRING into the current
   thread. Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *exec_string, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;
  char filename[NAME_MAX + 1];
  int fn_len;

  /* Extract filename (first token) from exec_string */
  fn_len = strcspn(exec_string, " ");
  strlcpy(filename, exec_string, fn_len + 1);

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (filename);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", filename);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", filename);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (exec_string, esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  return success;
}


/* load() helpers. */

static bool
install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
  user virtual memory. */
static bool
setup_stack (const char *exec_string, void **esp) 
{
  uint8_t *kpage;
  void *current = PHYS_BASE;
  void *stack_bottom = PHYS_BASE - PGSIZE;
  char *token, *save_ptr;
  char *argv[ARG_MAX];
  int argc = 0;
  size_t exec_str_len;
  size_t required_space;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage == NULL) 
    goto fail;

  if (!install_page (stack_bottom, kpage, true))
    goto fail;

  /*
  * We push the program's arguments and a fake return address onto the new user
  * stack to build a "fake interrupt/return" frame. `intr_exit` will pop the
  * CPU state from this fabricated frame and transfer control into the user 
  * program's main();
  *
  * On 32-bit x86 the ABI calling convention expects the return adress and the 
  * function's arguments on the stack. We must maintain 4-byte alignment to 
  * match the CPU's expectations when `intr_exit` restores SS, ESP, EFLAGS, CS 
  * and EIP.
  *
  * Minimal stack configuration (addresses low -> high):
  *
  *  [ PHYS_BASE - PGSIZE ]       // start of the user page
  *  < empty space >
  *  [ esp ]                      // start of the stack
  *  return address = 0           // fake "return" to userland
  *  argc                         // first main() argument
  *  argv                         // second main() argument
  *  argv[0]                      // pointer to program name string
  *  argv[1]                      // pointers to program's argument strings
  *  < ... >
  *  argv[argc-1]
  *  argv[argc] = \0              // argv is null-terminated
  *  padding = 0                  // 0–3 bytes to ensure 4-byte alignment
  *  < argument strings >         // null-terminated argument strings
  *  [ PHYS_BASE ]                // end of the stack / user page
  *
  */

  /* Push the entire exec_string onto the stack first */
  exec_str_len = strlen(exec_string) + 1;
  if ((char *)current - exec_str_len < (char *)stack_bottom)
    goto fail;
  current = (char *)current - exec_str_len;
  strlcpy((char *)current, exec_string, exec_str_len);

  /* Tokenize in-place on the stack and collect argv pointers */
  for (token = strtok_r((char *)current, " ", &save_ptr); 
       token != NULL && argc < ARG_MAX; 
       token = strtok_r(NULL, " ", &save_ptr)) {
    argv[argc++] = token;
  }
  
  if (argc == 0)
    goto fail;
  
  /* Word-align */
  current = (void *)((uintptr_t)current & ~3);

  /* Check if we have enough space for all stack data */
  required_space = sizeof(char *) +         /* NULL terminator */
                   argc * sizeof(char *) +  /* argv[] array */
                   sizeof(char **) +        /* argv pointer */
                   sizeof(int) +            /* argc */
                   sizeof(void *);          /* fake return address */
  if ((char *)current - required_space < (char *)stack_bottom)
    goto fail;

  // FIXME: all this casts are confusing. Review
  
  /* Push NULL terminator for argv[] array */
  current = (char **)current - 1;
  *(char **)current = NULL;
  
  /* Push argv[] array (pointers to argument strings) */
  current = (char **)current - argc;
  memcpy(current, argv, argc * sizeof(char *));

  /* Push argv (pointer to argv[] array) */
  char **argv_ptr = (char **)current;
  current = (char ***)current - 1;
  *(char ***)current = argv_ptr;
  
  /* Push argc */
  current = (int *)current - 1;
  *(int *)current = argc;
  
  /* Push fake return address */
  current = (void **)current - 1;
  *(void **)current = NULL;
  
  /* Set the stack pointer */
  *esp = current;

  // /* DEBUG print argc and argv */
  // {
  //   void **stack_ptr = (void **)*esp;
  //   int debug_argc = *(int *)(stack_ptr + 1);
  //   char **debug_argv = *(char ***)(stack_ptr + 2);
    
  //   printf("DEBUG: argc = %d\n", debug_argc);
  //   for (int i = 0; i < debug_argc; i++) {
  //     printf("DEBUG: argv[%d] = %s\n", i, debug_argv[i]);
  //   }
  // }
  
  return true;

fail:
  if (kpage != NULL) palloc_free_page (kpage);
  return false;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

/* Initialize a process_exec_status structure. */
static struct process_exec_status*
process_exec_status_init()
{
  struct process_exec_status *exec_status =  malloc(sizeof(struct process_exec_status));
  if (exec_status == NULL)
    return NULL;
  exec_status->pid = PID_ERROR;
  exec_status->exit_status = -1;
  exec_status->has_exited = false;
  exec_status->waited_on = false;
  exec_status->orphan = false;
  sema_init(&exec_status->wait_sema, 0);
  lock_init(&exec_status->lock); 
  return exec_status;
}
