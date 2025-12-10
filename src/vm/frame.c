#include "vm/frame.h"

#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "filesys/file.h"

// #define DEBUG
#include <debug.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static struct list frame_table;        /* List of all allocated frames */
struct lock frame_table_lock;          /* Lock for frame table operations */
static struct list_elem *clock_hand;   /* Clock hand for eviction algorithm */

static struct frame_table_entry *frame_table_evict_page(void);

/* Initialize the frame table and lock */
void frame_table_init(void)
{
  list_init(&frame_table);
  lock_init(&frame_table_lock);
  clock_hand = list_end(&frame_table);
}

/* Allocate a physical frame for a user page.
   Returns kernel virtual address of the frame (already pinned), or NULL on failure.
   The returned frame is pinned and assigned to spte.
   
   Caller must: populate the frame, install page mapping, then call frame_unpin(kpage). */
void *frame_alloc(struct sup_page_table_entry *spte)
{
  ASSERT(spte != NULL);
  
  lock_acquire(&frame_table_lock);

  /* 1. Try to allocate a fresh frame from palloc */
  void *kpage = palloc_get_page(PAL_USER | PAL_ZERO);
  if (kpage != NULL)
  {
    /* Create frame table entry for the new frame */
    struct frame_table_entry *fte = malloc(sizeof(struct frame_table_entry));
    if (fte == NULL)
    {
      /* Out of memory for frame struct - must free the page */
      palloc_free_page(kpage);
      lock_release(&frame_table_lock);
      return NULL;
    }

    fte->kpage = kpage;
    fte->spte = spte;
    fte->pagedir = thread_current()->pagedir;
    fte->pinned = true;  /* Return pinned */
    list_push_back(&frame_table, &fte->elem);
    DEBUG_PRINT("[frame_alloc] new frame: kpage=%p upage=%p pagedir=%p\n", kpage, spte->upage, fte->pagedir);
    lock_release(&frame_table_lock);
    return kpage;
  }

  /* 2. palloc failed - must evict a page and reuse its frame */
  struct frame_table_entry *victim = frame_table_evict_page();
  if (victim == NULL)
  {
    lock_release(&frame_table_lock);
    return NULL;  /* No frames available */
  }
  
  DEBUG_PRINT("[frame_alloc] reused frame kpage=%p old+pagedir=%p new_pagedir= old_upage=%p new_upage=%p\n",
               victim->kpage, victim->pagedir, thread_current()->pagedir, victim->spte->upage, spte->upage);

  memset(victim->kpage, 0, PGSIZE); /* Zero the page for the new owner */
  victim->spte = spte; /* Reassign victim frame to the new owner */
  victim->pagedir = thread_current()->pagedir; /* New owner page directory */
  
  lock_release(&frame_table_lock);
  return victim->kpage;
}

/* Free a frame by removing it from the frame table and freeing the physical page. */
void frame_free(void *kpage)
{
  ASSERT(kpage != NULL);
  
  lock_acquire(&frame_table_lock);

  struct list_elem *e;
  for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
  {
    struct frame_table_entry *fte = list_entry(e, struct frame_table_entry, elem);
    if (fte->kpage == kpage)
    {
      /* Remove frame from table and free physical page */
      DEBUG_PRINT("[frame_free] freeing kpage=%p upage=%p pagedir=%p\n", fte->kpage, fte->spte ? fte->spte->upage : NULL, fte->pagedir);
      list_remove(&fte->elem);
      if (fte->spte != NULL)
      {
        pagedir_clear_page(fte->pagedir, fte->spte->upage);
      }
      palloc_free_page(fte->kpage);
      free(fte);
      lock_release(&frame_table_lock);
      return;
    }
  }
  
  lock_release(&frame_table_lock);
  PANIC("frame_free: frame not found in frame table");
}

/* Evict a frame using Clock (Second-Chance) algorithm.
   Called when frame_alloc fails and no free memory available.
   Assumes: caller holds frame_table_lock.
   Returns: pointer to victim frame_table_entry (pinned and ready for reuse),
            or NULL if eviction failed (all frames pinned). */
static struct frame_table_entry *frame_table_evict_page(void)
{
  ASSERT(lock_held_by_current_thread(&frame_table_lock));
  ASSERT(!list_empty(&frame_table));

  struct frame_table_entry *victim = NULL;
  size_t iterations = 0;

  /* Clock algorithm:
     1. Start at clock_hand position
     2. For each frame:
        - Skip pinned frames
        - If accessed bit is 0, evict this frame
        - If accessed bit is 1, set it to 0 and move to next frame
     3. If no frame is found within 2 rounds, return NULL */
  while (victim == NULL && iterations < list_size(&frame_table) * 2)
  {
    if (clock_hand == NULL || clock_hand == list_end(&frame_table))
    {
      clock_hand = list_begin(&frame_table);
    }

    struct frame_table_entry *f = list_entry(clock_hand, struct frame_table_entry, elem);
    DEBUG_PRINT("[evict] inspect frame kpage=%p upage=%p pagedir=%p pinned=%d\n",
          f->kpage, f->spte ? f->spte->upage : NULL, f->pagedir, f->pinned);
    
    /* Skip pinned frames - they cannot be evicted */
    if (f->pinned)
    {
      clock_hand = list_next(clock_hand);
      iterations++;
      continue;
    }
    
    /* Check accessed bit */
    if (pagedir_is_accessed(f->pagedir, f->spte->upage))
    {
      /* Second chance: clear bit and move on */
      pagedir_set_accessed(f->pagedir, f->spte->upage, false);
      DEBUG_PRINT("[evict] second_chance clear accessed: upage=%p\n", f->spte->upage);
    }
    else
    {
      victim = f;
      victim->pinned = true;
      DEBUG_PRINT("[evict] selected victim: kpage=%p upage=%p pagedir=%p\n", victim->kpage, victim->spte->upage, victim->pagedir);
    }

    clock_hand = list_next(clock_hand);
    iterations++;
  }

  if (victim == NULL) { return NULL; }
  
  ASSERT(victim->spte != NULL);
  ASSERT(!is_user_vaddr(victim->kpage));

  /* Evict based on page type */
  if (victim->spte->type == PAGE_ANON)
  {
    /* All nonymous pages must be swapped out */
    victim->spte->swap_slot = swap_out(victim->kpage);
    victim->spte->type = PAGE_SWAP;
    DEBUG_PRINT("[evict] swapped out page: upage=%p kpage=%p slot=%zu\n", 
                victim->spte->upage, victim->kpage, victim->spte->swap_slot);
  }
  else if (victim->spte->type == PAGE_FILE)
  {
    /* File-backed pages: write back only if dirty and writable */
    if (pagedir_is_dirty(victim->pagedir, victim->spte->upage) && victim->spte->file_writable)
    {
      /* Debug: show first few bytes before writeback to help diagnose incorrect contents */
      DEBUG_PRINT("[evict] writing back first bytes: %02x %02x %02x %02x\n",
                  ((uint8_t *)victim->kpage)[0], ((uint8_t *)victim->kpage)[1], ((uint8_t *)victim->kpage)[2], ((uint8_t *)victim->kpage)[3]);
      file_write_at(victim->spte->file, victim->kpage, victim->spte->read_bytes, victim->spte->file_offset);
      DEBUG_PRINT("[evict] wrote dirty file page: upage=%p kpage=%p\n", 
                  victim->spte->upage, victim->kpage);
    }
    /* Clean file pages can be reloaded from file, so just evict */
  }

  /* Unmap from victim's page directory */
  pagedir_clear_page(victim->pagedir, victim->spte->upage);
  victim->spte = NULL; /* caller will assign new owner */
  /* victim->pinned remains true - caller owns it now */
  /* victim->kpage is reused by caller */

  return victim;
}

/* Pin a frame to prevent it from being evicted. */
void frame_pin(void *kpage)
{
  ASSERT(kpage != NULL);
  
  lock_acquire(&frame_table_lock);

  struct list_elem *e;
  for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
  {
    struct frame_table_entry *fte = list_entry(e, struct frame_table_entry, elem);
    if (fte->kpage == kpage)
    {
      fte->pinned = true;
      lock_release(&frame_table_lock);
      return;
    }
  }
  
  lock_release(&frame_table_lock);
  PANIC("frame_pin: frame not found in frame table");
}

/* Unpin a frame to allow it to be evicted. */
void frame_unpin(void *kpage)
{
  ASSERT(kpage != NULL);
  
  lock_acquire(&frame_table_lock);

  struct list_elem *e;
  for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
  {
    struct frame_table_entry *fte = list_entry(e, struct frame_table_entry, elem);
    if (fte->kpage == kpage)
    {
      fte->pinned = false;
      lock_release(&frame_table_lock);
      return;
    }
  }
  
  lock_release(&frame_table_lock);
  PANIC("frame_unpin: frame not found in frame table");
}

/* Return the spte associated with a given kernel kpage, or NULL if not found.
   Caller does not take ownership of the returned spte; do not free it here. */
struct sup_page_table_entry *frame_lookup_spte(void *kpage)
{
  ASSERT(kpage != NULL);
  struct sup_page_table_entry *spte = NULL;

  lock_acquire(&frame_table_lock);
  struct list_elem *e;
  for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
  {
    struct frame_table_entry *fte = list_entry(e, struct frame_table_entry, elem);
    if (fte->kpage == kpage)
    {
      spte = fte->spte;
      break;
    }
  }
  lock_release(&frame_table_lock);
  return spte;
}
