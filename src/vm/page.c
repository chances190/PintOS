#include "vm/page.h"

#include "devices/timer.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"

// #define DEBUG
#include <debug.h>
#include <string.h>
#include <stdio.h>

/* Validates that a page address is properly aligned.
   Returns true if valid, false otherwise.
   Note: Does not check is_user_vaddr as SPT can contain kernel pages in some cases. */
static bool validate_upage(void *upage)
{
  return upage != NULL && pg_ofs(upage) == 0;
}

/* Allocates and initializes a base SPT entry with common fields.
   Returns the allocated entry or NULL on failure. */
static struct sup_page_table_entry *allocate_spte(void *upage, enum page_type type, bool writable)
{
  ASSERT(validate_upage(upage));
  
  struct sup_page_table_entry *spte = malloc(sizeof(struct sup_page_table_entry));
  if (spte == NULL)
    return NULL;
  
  spte->upage = upage;
  spte->type = type;
  spte->swap_slot = 0;
  spte->file_writable = writable;
  
  return spte;
}

/* Initialize an empty supplemental page table */
void spt_init(struct list *spt)
{
  ASSERT(spt != NULL);
  list_init(spt);
}

/* Look up a page in the supplemental page table.
   Returns the entry if found, NULL otherwise.
   The vaddr is rounded down to page boundary before lookup. */
struct sup_page_table_entry *spt_lookup(struct list *spt, void *vaddr)
{
  ASSERT(spt != NULL);

  void *upage = pg_round_down(vaddr);
  struct list_elem *e;

  for (e = list_begin(spt); e != list_end(spt); e = list_next(e))
  {
    struct sup_page_table_entry *spte;
    spte = list_entry(e, struct sup_page_table_entry, elem);

    if (spte->upage == upage)
    {
      return spte;
    }
  }

  return NULL;
}

/* Destroy the supplemental page table and free all entries */
void spt_destroy(struct list *spt)
{
  ASSERT(spt != NULL);

  struct list_elem *e;

  while (!list_empty(spt))
  {
    e = list_pop_front(spt);
    struct sup_page_table_entry *spte = list_entry(e, struct sup_page_table_entry, elem);
    free(spte);
  }
}

/* Create a file-backed SPT entry (for executable segments or mmap).
   Returns allocated entry or NULL on failure. */
struct sup_page_table_entry *spt_create_page_file(
    void *upage, struct file *file, off_t offset, size_t read_bytes, size_t zero_bytes, bool writable)
{
  ASSERT(validate_upage(upage));
  ASSERT(file != NULL);
  ASSERT(read_bytes + zero_bytes == PGSIZE);
  
  struct sup_page_table_entry *spte = allocate_spte(upage, PAGE_FILE, writable);
  if (spte == NULL)
    return NULL;
  
  spte->file = file;
  spte->file_offset = offset;
  spte->read_bytes = read_bytes;
  spte->zero_bytes = zero_bytes;
  
  DEBUG_PRINT("[spt_create_page_file] created spte=%p upage=%p file=%p offset=%ld read=%zu zero=%zu writable=%d\n",
               spte, upage, file, (long) offset, read_bytes, zero_bytes, writable);
  
  return spte;
}

/* Create an anonymous (zero-backed) SPT entry (for stack, zero pages).
   Returns allocated entry or NULL on failure. */
struct sup_page_table_entry *spt_create_page_anon(void *upage, bool writable)
{
  ASSERT(validate_upage(upage));
  
  struct sup_page_table_entry *spte = allocate_spte(upage, PAGE_ANON, writable);
  if (spte == NULL)
    return NULL;
  
  spte->file = NULL;
  spte->file_offset = 0;
  spte->read_bytes = 0;
  spte->zero_bytes = PGSIZE;
  
  DEBUG_PRINT("[spt_create_page_anon] created spte=%p upage=%p writable=%d\n", spte, upage, writable);
  
  return spte;
}
