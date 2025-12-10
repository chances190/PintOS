#include "userprog/exception.h"

#include "filesys/file.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "vm/vm.h"


// #define DEBUG
#include <debug.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Stack growth validation constants */
#define USER_STACK_BASE 0x08048000  /* Minimum valid user virtual address */
#define STACK_TOLERANCE 32          /* Allow up to 32 bytes below ESP (for PUSHA) */
#define MAX_STACK_SIZE (8 * 1024 * 1024)  /* 8 MB maximum stack size */

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill(struct intr_frame *);
static void page_fault(struct intr_frame *);

/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void exception_init(void)
{
  /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
  intr_register_int(3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int(4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int(5, 3, INTR_ON, kill, "#BR BOUND Range Exceeded Exception");

  /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
  intr_register_int(0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int(1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int(6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int(7, 0, INTR_ON, kill, "#NM Device Not Available Exception");
  intr_register_int(11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int(12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int(13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int(16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int(19, 0, INTR_ON, kill, "#XF SIMD Floating-Point Exception");

  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int(14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void exception_print_stats(void) { printf("Exception: %lld page faults\n", page_fault_cnt); }

/* Handler for an exception (probably) caused by a user process. */
static void kill(struct intr_frame *f)
{
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */

  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs)
  {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
      printf("%s: dying due to interrupt %#04x (%s).\n", thread_name(), f->vec_no, intr_name(f->vec_no));
      intr_dump_frame(f);
      thread_exit(-1);

    case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame(f);
      PANIC("Kernel bug - unexpected interrupt in kernel");

    default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
      printf("Interrupt %#04x (%s) in unknown segment %04x\n", f->vec_no, intr_name(f->vec_no), f->cs);
      thread_exit(-1);
  }
}

/* Page fault handler.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void page_fault(struct intr_frame *f)
{
  bool not_present; /* True: page not present, false: protection violation */
  bool write;       /* True: access was write, false: access was read. */
  bool user;        /* True: access by user, false: access by kernel. */
  void *fault_addr; /* Fault address. */

  /* Obtain faulting address, the virtual address that was
     accessed to cause the fault.  It may point to code or to
     data.  It is not necessarily the address of the instruction
     that caused the fault (that's f->eip).
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
     (#PF)". */
  asm("movl %%cr2, %0" : "=r"(fault_addr));

  /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
  intr_enable();

  /* Count page faults. */
  page_fault_cnt++;

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;
  DEBUG_PRINT("[page_fault] fault_addr=%p, not_present=%d, write=%d, user=%d\n", fault_addr, not_present, write, user);

  
  /* Protection violation (page present but access not allowed) is fatal. */
  if (!not_present)
  {
    goto exception;
  }
  
  /* Invalid user addresses are fatal. */
  if (user && (fault_addr == NULL || is_kernel_vaddr(fault_addr)))
  {
    DEBUG_PRINT("[page_fault] Invalid user address: fault_addr=%p\n", fault_addr);
    goto exception;
  }
  
  /* If kernel tried to access user memory, attempt to recover via the
     user-mode error handler saved in %eax.  This replicates the Part 2
     behavior for safe pointer dereferences in the kernel. */
  if (!user && is_user_vaddr(fault_addr))
  {
    DEBUG_PRINT("[page_fault] User process tried to access invalid address: fault_addr=%p\n",
                fault_addr);
    DEBUG_PRINT("[page_fault] Setting eip=%p (from eax), eax=0xffffffff\n", (void *) f->eax);
    DEBUG_PRINT("[page_fault] kernel regs: eip=%p esp=%p eax=%p ebx=%p ecx=%p edx=%p\n",
                (void *) f->eip, (void *) f->esp, (void *) f->eax, (void *) f->ebx, (void *) f->ecx, (void *) f->edx);
    /* Set eip to eax (which contains the error handler address)
       and eax to 0xffffffff to signal error, then return. */
    f->eip = (void (*)(void)) f->eax;
    f->eax = 0xFFFFFFFF;
    return;
  }

  if (user && is_user_vaddr(fault_addr) && not_present)
  {
    struct thread *cur = thread_current();
    void *upage = pg_round_down(fault_addr);
    void *esp = f->esp;

    /* Check if page already exists in supplemental page table */
    struct sup_page_table_entry *spte = spt_lookup(&cur->sup_page_table, upage);
    if (spte == NULL)
    {
      /* Page doesn't exist in SPT - check if this is valid stack growth */
      
      /* Stack growth validation:
        1. Must be at or below current stack pointer (stack grows downward)
        2. Must be within STACK_TOLERANCE of stack pointer (handles PUSH/PUSHA)
        3. Must not go below USER_STACK_BASE
        Note: fault_addr <= esp (not <) to handle PUSH which faults at ESP before decrement */
      bool is_valid_stack_growth = (fault_addr <= esp && 
                                    fault_addr >= esp - STACK_TOLERANCE &&
                                    fault_addr >= (void *) USER_STACK_BASE);
      
      if (!is_valid_stack_growth)
      {
        DEBUG_PRINT("[page_fault] Not a valid stack growth: fault_addr=%p, esp=%p\n", 
                    fault_addr, esp);
        goto exception;
      }

      /* Valid stack growth - allocate page */
      DEBUG_PRINT("[page_fault] Stack growth: allocating page at %p\n", upage);

      /* Allocate a new zero-filled page for the stack (creates SPT entry + frame atomically) */
      uint8_t *kpage = vm_palloc(upage, true);
      if (kpage == NULL)
      {
        DEBUG_PRINT("[page_fault] vm_palloc failed (out of memory)\n");
        goto exception;
      }

      DEBUG_PRINT("[page_fault] Stack growth successful\n");
      return;
    }
    else /* spte != NULL */
    {
      /* Page exists in SPT - load it */
      DEBUG_PRINT("[page_fault] Page found in SPT: upage=%p, type=%d, file=%p\n", 
                  upage, spte->type, spte->file);

      /* Load page from disk */
      uint8_t *kpage = vm_load(upage);
      if (kpage == NULL)
      {
        DEBUG_PRINT("[page_fault] vm_load failed\n");
        goto exception;
      }

      DEBUG_PRINT("[page_fault] Page loaded successfully to %p\n", kpage);
      return;
    }
  }


exception:
  DEBUG_PRINT("[page_fault] Unhandled page fault, killing process\n");
  printf("Page fault at %p: %s error %s page in %s context.\n", 
         fault_addr, 
         not_present ?  "not present" : "rights violation", 
         write ? "writing" : "reading",
         user ? "user" : "kernel");
  kill(f);
}
