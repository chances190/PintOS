#include "vm/vm.h"

#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "userprog/fdtable.h"

// #define DEBUG
#include <debug.h>
#include <string.h>
#include <stdio.h>

void vm_init(void)
{
  frame_table_init();
}

/* Load a page from disk (file or swap) into physical memory.
   Allocates frame, loads data, and maps into page directory.
   Returns kernel virtual address of frame, or NULL on failure. */
void *vm_load(void *upage)
{
  ASSERT(upage != NULL);
  ASSERT(is_user_vaddr(upage));
  ASSERT(pg_ofs(upage) == 0);  /* Must be page-aligned */

  struct thread *t = thread_current();

  struct sup_page_table_entry *spte = spt_lookup(&t->sup_page_table, upage);
  ASSERT(spte != NULL);
  DEBUG_PRINT("[vm_load] upage=%p spte=%p type=%d file=%p offset=%ld read_bytes=%zu zero_bytes=%zu\n",
               upage, spte, spte->type, spte->file, (long) spte->file_offset, spte->read_bytes, spte->zero_bytes);
  
  /* Allocate physical frame (returned pinned) */
  void *kpage = frame_alloc(spte);
  if (kpage == NULL) {
    return NULL;
  }
  
  /* Load page content based on type */
  if (spte->type == PAGE_SWAP)
  {
    DEBUG_PRINT("[vm_load] swap_in slot=%zu for upage=%p kpage=%p\n", spte->swap_slot, upage, kpage);
    swap_in(spte->swap_slot, kpage);
    spte->type = PAGE_ANON;
  }
  else if (spte->type == PAGE_FILE)
  {
    /* Load from file (executable segment or mmap) */
    off_t bytes_read = file_read_at(spte->file, kpage, spte->read_bytes, spte->file_offset);
    DEBUG_PRINT("[vm_load] file_read_at bytes=%d (expected %zu) upage=%p file=%p\n", bytes_read, spte->read_bytes, upage, spte->file);
    DEBUG_PRINT("[vm_load] read bytes: %02x %02x %02x %02x\n", ((uint8_t *)kpage)[0], ((uint8_t *)kpage)[1], ((uint8_t *)kpage)[2], ((uint8_t *)kpage)[3]);

    if (bytes_read != (off_t) spte->read_bytes)
    {
      /* File read failed */
      frame_unpin(kpage);
      return NULL;
    }
    /* Zero remaining bytes */
    memset(kpage + spte->read_bytes, 0, spte->zero_bytes);
  }
  
  /* Determine writability based on page type */
  bool writable = (spte->type == PAGE_FILE) ? spte->file_writable : true;
  
  /* Map into page directory */
  if (!pagedir_map_page(t->pagedir, spte->upage, kpage, writable)) {
    frame_unpin(kpage);
    return NULL;
  }
  DEBUG_PRINT("[vm_load] mapped upage=%p -> kpage=%p writable=%d\n", spte->upage, kpage, writable);
  
  /* Unpin frame - it's now ready for eviction if needed */
  frame_unpin(kpage);
  return kpage;
}

/* Allocate a physical frame for a new user page.
   Returns: kernel virtual address of frame, or NULL on failure. */
void *vm_palloc(void *upage, bool writable)
{
  ASSERT(upage != NULL);
  ASSERT(is_user_vaddr(upage));
  ASSERT(pg_ofs(upage) == 0);  /* Must be page-aligned */

  struct thread *t = thread_current();

  ASSERT(spt_lookup(&t->sup_page_table, upage) == NULL);

  /* Create anonymous (zero-backed) SPT entry */
  struct sup_page_table_entry *spte = spt_create_page_anon(upage, writable);
  if (spte == NULL) return NULL;
  DEBUG_PRINT("[vm_palloc] created anon spte=%p upage=%p writable=%d\n", spte, upage, writable);

  /* Allocate frame (returned pinned and zeroed) */
  void *kpage = frame_alloc(spte);
  if (kpage == NULL)
  {
    free(spte);
    return NULL;
  }

  list_push_back(&t->sup_page_table, &spte->elem);

  /* Map into page directory */
  if (!pagedir_map_page(t->pagedir, upage, kpage, writable))
  {
    /* Mapping failed, undo everything */
    list_remove(&spte->elem);
    frame_unpin(kpage);
    free(spte);
    return NULL;
  }

  /* Unpin frame - it's now ready for eviction if needed */
  frame_unpin(kpage);
  return kpage;
}

/* Free a physical frame and remove it from the process' SPT. */
void vm_free(void *kpage)
{
  ASSERT(kpage != NULL);
  ASSERT(pg_ofs(kpage) == 0);

  /* Find SPT entry via frame table (kpage -> spte), which avoids scanning the SPT. */
  struct sup_page_table_entry *spte = frame_lookup_spte(kpage);
  if (spte == NULL) { return; } /* Page not allocated */

  /* Free swap slot if page was evicted */
  if (spte->type == PAGE_SWAP)
  {
    swap_free(spte->swap_slot);
  }

  /* Remove from frame table (if tracked separately) */
  frame_free(kpage);

  /* Remove from SPT */
  list_remove(&spte->elem);
}

/* Unmap a memory-mapped region for a thread, flush dirty pages to file, and free resources. */
void vm_munmap(struct thread *t, mapid_t mapping)
{
  ASSERT(t != NULL);
  struct list_elem *e;
  for (e = list_begin(&t->mappings); e != list_end(&t->mappings); e = list_next(e))
  {
    struct mmap_region *m = list_entry(e, struct mmap_region, elem);
    if (m->mapid == mapping)
    {
      for (size_t i = 0; i < m->page_cnt; i++)
      {
        void *upage = (uint8_t *) m->start + i * PGSIZE;
        struct sup_page_table_entry *spte = spt_lookup(&t->sup_page_table, upage);
        if (spte == NULL) continue;

        void *kpage = pagedir_get_page(t->pagedir, upage);
        if (kpage != NULL)
        {
          if (pagedir_is_dirty(t->pagedir, upage) && spte->file_writable)
          {
            lock_acquire(&filesys_lock);
            file_write_at(m->file, kpage, spte->read_bytes, spte->file_offset);
            lock_release(&filesys_lock);
          }
          pagedir_clear_page(t->pagedir, upage);
          frame_free(kpage);
        }
        if (spte->type == PAGE_SWAP)
        {
          swap_free(spte->swap_slot);
        }
        list_remove(&spte->elem);
        free(spte);
      }
      lock_acquire(&filesys_lock);
      file_close(m->file);
      lock_release(&filesys_lock);
      list_remove(e);
      free(m);
      return;
    }
  }
}

/* Create a memory-mapped region for a thread and lazily populate the supplemental page table.
   Returns mapping id on success, MAP_FAILED on error. */
mapid_t vm_mmap(struct thread *t, int fd, void *addr)
{
  /* Validate address & fd */
  if (addr == NULL || !is_user_vaddr(addr) || pg_ofs(addr) != 0)
  {
    return MAP_FAILED;
  }

  if (fd < 2)
  {
    return MAP_FAILED;
  }

  struct file *orig = fd_table_get(fd);
  if (orig == NULL)
  {
    return MAP_FAILED;
  }

  /* Duplicate file handle for mapping lifecycle */
  struct file *file = file_reopen(orig);
  if (file == NULL)
  {
    return MAP_FAILED;
  }

  off_t len;
  lock_acquire(&filesys_lock);
  len = file_length(file);
  lock_release(&filesys_lock);

  if (len == 0)
  {
    file_close(file);
    return MAP_FAILED;
  }

  size_t page_cnt = (len + PGSIZE - 1) / PGSIZE;

  /* Validate range and ensure no overlaps */
  for (size_t i = 0; i < page_cnt; i++)
  {
    void *upage = (uint8_t *) addr + i * PGSIZE;
    if (!is_user_vaddr(upage) || spt_lookup(&t->sup_page_table, upage) != NULL)
    {
      file_close(file);
      return MAP_FAILED;
    }
  }

  /* Create mapping descriptor */
  struct mmap_region *m = malloc(sizeof(*m));
  if (m == NULL)
  {
    file_close(file);
    return MAP_FAILED;
  }
  m->mapid = t->next_mapid++;
  m->start = addr;
  m->page_cnt = page_cnt;
  m->file = file;
  list_push_back(&t->mappings, &m->elem);

  /* Populate SPT entries for each page (lazy load) */
  off_t offset = 0;
  for (size_t i = 0; i < page_cnt; i++)
  {
    void *upage = (uint8_t *) addr + i * PGSIZE;
    size_t read_bytes = (len - offset) < PGSIZE ? (size_t)(len - offset) : PGSIZE;
    size_t zero_bytes = PGSIZE - read_bytes;

    struct sup_page_table_entry *spte = spt_create_page_file(upage, file, offset, read_bytes, zero_bytes, true);
    if (spte == NULL)
    {
      /* cleanup on failure */
      vm_munmap(t, m->mapid);
      return MAP_FAILED;
    }
    list_push_back(&t->sup_page_table, &spte->elem);
    offset += read_bytes;
  }

  return m->mapid;
}
