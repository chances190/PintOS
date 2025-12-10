#include "vm/swap.h"

#include "devices/block.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

#include <bitmap.h>
#include <debug.h>
#include <stdio.h>
#include <stdint.h>


static struct block *swap_block; /* Swap block device */

static struct bitmap *swap_table; /* Swap table - bitmap tracking free swap slots */

static struct lock swap_lock; /* Lock for swap operations */

/* Number of sectors per page (1 page = 4KB, 1 sector = 512 bytes) */
#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

/* Initialize swap table using the swap block device */
void swap_init(void)
{
  swap_block = block_get_role(BLOCK_SWAP);
  if (swap_block == NULL)
  {
    PANIC("No swap device found - cannot initialize swap space");
  }

  size_t swap_pages = block_size(swap_block) / SECTORS_PER_PAGE;

  swap_table = bitmap_create(swap_pages);
  if (swap_table == NULL)
  {
    PANIC("Failed to create swap table bitmap");
  }

  /* Initialize all slots as free */
  bitmap_set_all(swap_table, false);

  /* Initialize swap lock */
  lock_init(&swap_lock);

  printf("Swap: initialized %zu pages (%zu KB)\n", 
         swap_pages, swap_pages * PGSIZE / 1024);
}

/* Write a page to swap.
   Returns the swap slot index where the page was written. */
size_t swap_out(void *kpage)
{
  ASSERT(kpage != NULL);
  ASSERT(pg_ofs(kpage) == 0); /* Must be page-aligned */

  lock_acquire(&swap_lock);

  /* Find a free swap slot */
  size_t swap_index = bitmap_scan_and_flip(swap_table, 0, 1, false);
  if (swap_index == BITMAP_ERROR)
  {
    PANIC("Swap is full - cannot swap out page");
  }

  /* Calculate starting sector for this swap slot */
  block_sector_t sector = swap_index * SECTORS_PER_PAGE;

  uint32_t sample = *(uint32_t *) kpage;
  DEBUG_PRINT("[swap_out] slot=%zu sample=0x%x", swap_index, sample);

  /* Write page to swap, one sector at a time */
  uint8_t *src = kpage;
  for (size_t i = 0; i < SECTORS_PER_PAGE; i++)
  {
    block_write(swap_block, sector + i, src + i * BLOCK_SECTOR_SIZE);
  }

  lock_release(&swap_lock);

  return swap_index;
}

/* Read a page from swap into memory and 
   mark its corresponding swap slot as free */
void swap_in(size_t swap_index, void *kpage)
{
  ASSERT(kpage != NULL);
  ASSERT(pg_ofs(kpage) == 0); /* Must be page-aligned */

  lock_acquire(&swap_lock);

  ASSERT(bitmap_test(swap_table, swap_index)); /* Page must be present on swap */

  /* Calculate starting sector for this swap slot */
  block_sector_t sector = swap_index * SECTORS_PER_PAGE;

  /* Read page from swap, one sector at a time */
  uint8_t *dst = kpage;
  for (size_t i = 0; i < SECTORS_PER_PAGE; i++)
  {
    block_read(swap_block, sector + i, dst + i * BLOCK_SECTOR_SIZE);
  }

  uint32_t sample = *(uint32_t *) kpage;
  DEBUG_PRINT("[swap_in] slot=%zu sample=0x%x", swap_index, sample);

  /* Mark slot as free */
  bitmap_set(swap_table, swap_index, false);

  lock_release(&swap_lock);
}

/* Mark a swap slot as free without reading the page.
   Used when a swapped page is being deallocated. */
void swap_free(size_t swap_index)
{
  lock_acquire(&swap_lock);

  ASSERT(bitmap_test(swap_table, swap_index)); /* Slot must be occupied */
  bitmap_set(swap_table, swap_index, false);

  lock_release(&swap_lock);
}
