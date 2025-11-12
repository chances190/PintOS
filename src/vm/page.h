#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "filesys/off_t.h"
#include "threads/synch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Forward declaration */
struct file;

/* Supplemental page table entry - stores metadata about a page */
struct sup_page_table_entry
{
    void *user_vaddr; /* User virtual address (page-aligned) */

    /* Access tracking for eviction (LRU) */
    uint64_t access_time; /* Last access time (timer_ticks) */

    /* Page state flags */
    bool dirty;    /* Has page been modified? */
    bool accessed; /* Has page been accessed recently? */
    bool writable; /* Is page writable? */

    /* Swap information */
    bool is_swapped;   /* Is page currently in swap? */
    size_t swap_index; /* Swap slot index (if swapped) */

    /* Memory-mapped file information */
    bool is_mmap; /* Is this a memory-mapped file page? */

    /* File-backed page information (for lazy loading and mmap) */
    struct file *file; /* File to read from (NULL if not file-backed) */
    off_t offset;      /* Offset in file */
    size_t read_bytes; /* Bytes to read from file */
    size_t zero_bytes; /* Bytes to zero after read_bytes */

    /* List element for thread's supplemental page table */
    struct list_elem elem;
};

/* Initialize a supplemental page table (list) */
void spt_init(struct list *spt);

/* Insert an entry into the supplemental page table */
bool spt_insert(struct list *spt, struct sup_page_table_entry *spte);

/* Look up a page in the supplemental page table by virtual address */
struct sup_page_table_entry *spt_lookup(struct list *spt, void *vaddr);

/* Delete an entry from the supplemental page table */
void spt_delete(struct list *spt, void *vaddr);

/* Destroy the supplemental page table and free all entries */
void spt_destroy(struct list *spt);

/* Update the access time of a page */
void spt_update_access_time(struct list *spt, void *vaddr);

#endif /* vm/page.h */
