#include "vm/frame.h"

#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

#include <debug.h>
#include <stdio.h>

/* Global frame table - list of all allocated frames */
static struct list frame_table;

/* Lock for frame table operations */
struct lock frame_table_lock;

/* Initialize the frame table and lock */
void frame_table_init(void)
{
  list_init(&frame_table);
  lock_init(&frame_table_lock);
}

/* Allocate a frame for the given user page.
   Returns kernel virtual address of the frame, or NULL on failure.

   This is a wrapper around palloc_get_page that also tracks the frame
   in our frame table for future eviction.

   For now, if palloc fails (no frames available), we return NULL.
   Later, we will implement eviction to free up a frame. */
void *frame_alloc(enum palloc_flags flags, void *upage UNUSED)
{
  ASSERT(flags & PAL_USER); /* Must be user page */

  lock_acquire(&frame_table_lock);

  /* Try to allocate a page from the user pool */
  void *frame = palloc_get_page(flags);

  if (frame != NULL)
  {
    /* Create frame table entry */
    struct frame_table_entry *fte = malloc(sizeof(struct frame_table_entry));

    if (fte == NULL)
    {
      /* Out of memory for frame table entry */
      palloc_free_page(frame);
      lock_release(&frame_table_lock);
      return NULL;
    }

    /* Initialize frame table entry */
    fte->frame = frame;
    fte->owner = thread_current();
    fte->spte = NULL; /* Will be set later by caller if needed */

    /* Add to frame table */
    list_push_back(&frame_table, &fte->elem);
  }

  lock_release(&frame_table_lock);

  /* TODO: If frame is NULL (no memory), we should evict a page.
     For now, we just return NULL and let the caller handle it. */

  return frame;
}

/* Free a frame and remove it from the frame table */
void frame_free(void *frame)
{
  ASSERT(frame != NULL);

  lock_acquire(&frame_table_lock);

  /* Find and remove the frame table entry */
  struct list_elem *e;
  for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
  {
    struct frame_table_entry *fte = list_entry(e, struct frame_table_entry, elem);

    if (fte->frame == frame)
    {
      list_remove(e);
      free(fte);
      break;
    }
  }

  /* Free the actual page */
  palloc_free_page(frame);

  lock_release(&frame_table_lock);
}
