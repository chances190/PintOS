#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/synch.h"
#include <list.h>
#include <stdbool.h>
#include <stddef.h>

/* Forward declarations */
struct thread;
struct sup_page_table_entry;

extern struct lock frame_table_lock;   /* Lock for frame table operations */

/* Frame table entry - tracks a physical frame allocated to a user page */
struct frame_table_entry
{
    void *kpage;                            /* Physical frame address (kernel virtual addr) */
    struct sup_page_table_entry *spte;      /* SPT entry that owns this frame */
    uint32_t *pagedir;                      /* Owning page directory */
    bool pinned;                            /* If true, frame cannot be evicted */
    struct list_elem elem;                  /* List element for frame_table */
};

void frame_table_init(void);
void *frame_alloc(struct sup_page_table_entry *spte);
void frame_free(void *kpage);
void frame_pin(void *kpage);
void frame_unpin(void *kpage);
/* Look up the SPT entry for the given kernel page. Returns the spte or NULL. */
struct sup_page_table_entry *frame_lookup_spte(void *kpage);

#endif /* VM_FRAME_H */
