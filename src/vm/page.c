#include "vm/page.h"

#include "devices/timer.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"

#include <debug.h>
#include <string.h>

/* Initialize an empty supplemental page table */
void spt_init(struct list *spt)
{
  ASSERT(spt != NULL);
  list_init(spt);
}

/* Insert an entry into the supplemental page table.
   Returns true on success, false if entry already exists. */
bool spt_insert(struct list *spt, struct sup_page_table_entry *spte)
{
  ASSERT(spt != NULL);
  ASSERT(spte != NULL);
  ASSERT(pg_ofs(spte->user_vaddr) == 0); /* Must be page-aligned */

  /* Check if entry already exists */
  if (spt_lookup(spt, spte->user_vaddr) != NULL)
  {
    return false;
  }

  /* Add to list */
  list_push_back(spt, &spte->elem);
  return true;
}

/* Look up a page in the supplemental page table.
   Returns the entry if found, NULL otherwise.
   The vaddr is rounded down to page boundary before lookup. */
struct sup_page_table_entry *spt_lookup(struct list *spt, void *vaddr)
{
  ASSERT(spt != NULL);

  void *page = pg_round_down(vaddr);
  struct list_elem *e;

  for (e = list_begin(spt); e != list_end(spt); e = list_next(e))
  {
    struct sup_page_table_entry *spte;
    spte = list_entry(e, struct sup_page_table_entry, elem);

    if (spte->user_vaddr == page)
    {
      return spte;
    }
  }

  return NULL;
}

/* Delete an entry from the supplemental page table.
   Frees the entry's memory. */
void spt_delete(struct list *spt, void *vaddr)
{
  ASSERT(spt != NULL);

  struct sup_page_table_entry *spte = spt_lookup(spt, vaddr);

  if (spte != NULL)
  {
    list_remove(&spte->elem);
    free(spte);
  }
}

/* Destroy the supplemental page table and free all entries */
void spt_destroy(struct list *spt)
{
  ASSERT(spt != NULL);

  struct list_elem *e;

  while (!list_empty(spt))
  {
    e = list_pop_front(spt);
    struct sup_page_table_entry *spte;
    spte = list_entry(e, struct sup_page_table_entry, elem);
    free(spte);
  }
}

/* Update the access time of a page to current time */
void spt_update_access_time(struct list *spt, void *vaddr)
{
  ASSERT(spt != NULL);

  struct sup_page_table_entry *spte = spt_lookup(spt, vaddr);

  if (spte != NULL)
  {
    spte->access_time = timer_ticks();
  }
}
