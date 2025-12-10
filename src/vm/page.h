#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "filesys/off_t.h"
#include "userprog/process.h"
#include "threads/synch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Forward declarations */
struct file;
struct thread;

/* Page backing type - determines where page data comes from */
enum page_type
{
    PAGE_FILE,      /* File-backed: executable segments, mmap */
    PAGE_ANON,      /* Anonymous: stack, zero pages */
    PAGE_SWAP       /* Currently swapped out (preserves original type) */
};

/* Supplemental page table entry - stores metadata about a page */
struct sup_page_table_entry
{
    void *upage;      /* Virtual page address (user virtual address) */
    
    enum page_type type;   /* Current page type/state */
    
    /* Swap information (valid when type == PAGE_SWAP) */
    size_t swap_slot;     /* Swap slot index */
    
    /* File-backed page information (valid when type == PAGE_FILE) */
    struct file *file;     /* File to read from */
    off_t file_offset;     /* Offset in file */
    size_t read_bytes;     /* Bytes to read from file */
    size_t zero_bytes;     /* Bytes to zero after read_bytes */
    bool file_writable;    /* Is page writable? */

    /* List element for thread's supplemental page table */
    struct list_elem elem;
};

/* Memory-mapped region descriptor */
struct mmap_region
{
    mapid_t mapid;        /* Mapping id */
    void *start;          /* Starting user address */
    size_t page_cnt;      /* Number of pages mapped */
    struct file *file;    /* Backing file (reopened) */
    struct list_elem elem; /* List element for per-thread mappings */
};

void spt_init(struct list *spt);
bool spt_insert(struct list *spt, struct sup_page_table_entry *spte);
struct sup_page_table_entry *spt_lookup(struct list *spt, void *vaddr);
void spt_delete(struct list *spt, void *vaddr);
void spt_destroy(struct list *spt);

/* Helper functions to create SPT entries */
struct sup_page_table_entry *spt_create_page_file(
    void *upage, struct file *file, off_t offset, size_t read_bytes, size_t zero_bytes, bool writable);
struct sup_page_table_entry *spt_create_page_anon(void *upage, bool writable);

#endif /* VM_PAGE_H */
