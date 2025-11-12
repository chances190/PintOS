#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"
#include "threads/synch.h"

#include <stdbool.h>
#include <stdint.h>

/* Forward declaration */
struct thread;
struct sup_page_table_entry;

/* Frame table entry - tracks a physical frame allocated to a user page */
struct frame_table_entry
{
    void *frame;                       /* Physical frame address (kernel virtual addr) */
    struct thread *owner;              /* Thread that owns this frame */
    struct sup_page_table_entry *spte; /* Link to supplemental page table entry */
    struct list_elem elem;             /* List element for frame_table */
};

/* Global frame table lock - protects all frame table operations */
extern struct lock frame_table_lock;

/* Initialize the frame table */
void frame_table_init(void);

/* Allocate a frame with given flags for user virtual address upage */
void *frame_alloc(enum palloc_flags flags, void *upage);

/* Free a frame */
void frame_free(void *frame);

#endif /* vm/frame.h */
