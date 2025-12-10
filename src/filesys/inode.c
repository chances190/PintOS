#include "filesys/inode.h"

#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"

#include <debug.h>
#include <list.h>
#include <round.h>
#include <string.h>

/* Identifies an inode. */
#define INODE_MAGIC 0x494E4F44

/* Number of direct, indirect, and doubly indirect blocks */
#define DIRECT_BLOCKS 10
#define INDIRECT_BLOCKS 1
#define DOUBLE_INDIRECT_BLOCKS 1
#define TOTAL_BLOCK_PTRS (DIRECT_BLOCKS + INDIRECT_BLOCKS + DOUBLE_INDIRECT_BLOCKS)

/* Number of pointers per indirect block */
#define PTRS_PER_BLOCK (BLOCK_SECTOR_SIZE / sizeof(block_sector_t))

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
    block_sector_t blocks[TOTAL_BLOCK_PTRS]; /* Block pointers: 10 direct, 1 indirect, 1 double indirect */
    off_t length;                             /* File size in bytes. */
    bool is_dir;                              /* True if directory, false if file */
    unsigned magic;                           /* Magic number. */
    uint32_t unused[113];                     /* Not used - adjusted for new fields */
};

// mostra um erro na compilação se o inode_disk tiver um tamanho diferente de
// 512 bytes
_Static_assert(sizeof(struct inode_disk) == 512, "inode_disk must be 512 bytes");

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t bytes_to_sectors(off_t size) { return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE); }

/* Allocates a block sector and zeros it. Returns the sector number or -1 on failure. */
static block_sector_t allocate_block(void)
{
    block_sector_t sector = 0;
    if (free_map_allocate(1, &sector))
    {
        static char zeros[BLOCK_SECTOR_SIZE];
        block_write(fs_device, sector, zeros);
        return sector;
    }
    return (block_sector_t)-1;
}

/* Frees a block sector if it's valid. */
static void free_block(block_sector_t sector)
{
    if (sector != (block_sector_t)-1 && sector != 0)
    {
        free_map_release(sector, 1);
    }
}

/* Gets the data block sector for a given block index in an inode.
   Returns -1 if the block doesn't exist.
   If ALLOCATE is true, allocates new blocks as needed. */
static block_sector_t get_data_block(struct inode_disk *inode_disk, size_t block_idx, bool allocate)
{
    ASSERT(inode_disk != NULL);

    block_sector_t result = (block_sector_t)-1;

    /* Direct blocks */
    if (block_idx < DIRECT_BLOCKS)
    {
        if (inode_disk->blocks[block_idx] == 0 && allocate)
        {
            inode_disk->blocks[block_idx] = allocate_block();
        }
        result = inode_disk->blocks[block_idx];
    }
    /* Indirect block */
    else if (block_idx < DIRECT_BLOCKS + PTRS_PER_BLOCK)
    {
        size_t indirect_idx = block_idx - DIRECT_BLOCKS;
        block_sector_t *indirect_block = NULL;

        /* Allocate indirect block if needed */
        if (inode_disk->blocks[DIRECT_BLOCKS] == 0)
        {
            if (allocate)
            {
                inode_disk->blocks[DIRECT_BLOCKS] = allocate_block();
            }
            else
            {
                return (block_sector_t)-1;
            }
        }

        if (inode_disk->blocks[DIRECT_BLOCKS] != 0)
        {
            indirect_block = malloc(BLOCK_SECTOR_SIZE);
            if (indirect_block != NULL)
            {
                block_read(fs_device, inode_disk->blocks[DIRECT_BLOCKS], indirect_block);

                if (indirect_block[indirect_idx] == 0 && allocate)
                {
                    indirect_block[indirect_idx] = allocate_block();
                    block_write(fs_device, inode_disk->blocks[DIRECT_BLOCKS], indirect_block);
                }

                result = indirect_block[indirect_idx];
                free(indirect_block);
            }
        }
    }
    /* Double indirect block */
    else if (block_idx < DIRECT_BLOCKS + PTRS_PER_BLOCK + PTRS_PER_BLOCK * PTRS_PER_BLOCK)
    {
        size_t double_indirect_offset = block_idx - DIRECT_BLOCKS - PTRS_PER_BLOCK;
        size_t first_level_idx = double_indirect_offset / PTRS_PER_BLOCK;
        size_t second_level_idx = double_indirect_offset % PTRS_PER_BLOCK;

        block_sector_t *double_indirect_block = NULL;
        block_sector_t *indirect_block = NULL;

        /* Allocate double indirect block if needed */
        if (inode_disk->blocks[DIRECT_BLOCKS + 1] == 0)
        {
            if (allocate)
            {
                inode_disk->blocks[DIRECT_BLOCKS + 1] = allocate_block();
            }
            else
            {
                return (block_sector_t)-1;
            }
        }

        if (inode_disk->blocks[DIRECT_BLOCKS + 1] != 0)
        {
            double_indirect_block = malloc(BLOCK_SECTOR_SIZE);
            if (double_indirect_block != NULL)
            {
                block_read(fs_device, inode_disk->blocks[DIRECT_BLOCKS + 1], double_indirect_block);

                /* Allocate first level indirect block if needed */
                if (double_indirect_block[first_level_idx] == 0)
                {
                    if (allocate)
                    {
                        double_indirect_block[first_level_idx] = allocate_block();
                        block_write(fs_device, inode_disk->blocks[DIRECT_BLOCKS + 1], double_indirect_block);
                    }
                    else
                    {
                        free(double_indirect_block);
                        return (block_sector_t)-1;
                    }
                }

                if (double_indirect_block[first_level_idx] != 0)
                {
                    indirect_block = malloc(BLOCK_SECTOR_SIZE);
                    if (indirect_block != NULL)
                    {
                        block_read(fs_device, double_indirect_block[first_level_idx], indirect_block);

                        if (indirect_block[second_level_idx] == 0 && allocate)
                        {
                            indirect_block[second_level_idx] = allocate_block();
                            block_write(fs_device, double_indirect_block[first_level_idx], indirect_block);
                        }

                        result = indirect_block[second_level_idx];
                        free(indirect_block);
                    }
                }
                free(double_indirect_block);
            }
        }
    }

    return result;
}

/* Frees all data blocks in an inode recursively. */
static void free_inode_blocks(struct inode_disk *inode_disk)
{
    if (inode_disk == NULL)
        return;

    size_t i, j;

    /* Free direct blocks */
    for (i = 0; i < DIRECT_BLOCKS; i++)
    {
        free_block(inode_disk->blocks[i]);
    }

    /* Free indirect block and its data blocks */
    if (inode_disk->blocks[DIRECT_BLOCKS] != 0)
    {
        block_sector_t *indirect_block = malloc(BLOCK_SECTOR_SIZE);
        if (indirect_block != NULL)
        {
            block_read(fs_device, inode_disk->blocks[DIRECT_BLOCKS], indirect_block);
            for (i = 0; i < PTRS_PER_BLOCK; i++)
            {
                free_block(indirect_block[i]);
            }
            free(indirect_block);
        }
        free_block(inode_disk->blocks[DIRECT_BLOCKS]);
    }

    /* Free double indirect block and its data blocks */
    if (inode_disk->blocks[DIRECT_BLOCKS + 1] != 0)
    {
        block_sector_t *double_indirect_block = malloc(BLOCK_SECTOR_SIZE);
        if (double_indirect_block != NULL)
        {
            block_read(fs_device, inode_disk->blocks[DIRECT_BLOCKS + 1], double_indirect_block);
            for (i = 0; i < PTRS_PER_BLOCK; i++)
            {
                if (double_indirect_block[i] != 0)
                {
                    block_sector_t *indirect_block = malloc(BLOCK_SECTOR_SIZE);
                    if (indirect_block != NULL)
                    {
                        block_read(fs_device, double_indirect_block[i], indirect_block);
                        for (j = 0; j < PTRS_PER_BLOCK; j++)
                        {
                            free_block(indirect_block[j]);
                        }
                        free(indirect_block);
                    }
                    free_block(double_indirect_block[i]);
                }
            }
            free(double_indirect_block);
        }
        free_block(inode_disk->blocks[DIRECT_BLOCKS + 1]);
    }
}

/* In-memory inode. */
struct inode
{
    struct list_elem elem;  /* Element in inode list. */
    block_sector_t sector;  /* Sector number of disk location. */
    int open_cnt;           /* Number of openers. */
    bool removed;           /* True if deleted, false otherwise. */
    int deny_write_cnt;     /* 0: writes ok, >0: deny writes. */
    struct inode_disk data; /* Inode content. */
};

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t byte_to_sector(const struct inode *inode, off_t pos)
{
    ASSERT(inode != NULL);
    if (pos < inode->data.length)
    {
        size_t block_idx = pos / BLOCK_SECTOR_SIZE;
        return get_data_block((struct inode_disk *)&inode->data, block_idx, false);
    }
    else
    {
        return -1;
    }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Lock to protect open_inodes list during concurrent access. */
static struct lock inode_lock;

/* Initializes the inode module. */
void inode_init(void)
{
  list_init(&open_inodes);
  lock_init(&inode_lock);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool inode_create(block_sector_t sector, off_t length)
{
    struct inode_disk *disk_inode = NULL;
    bool success = false;

    ASSERT(length >= 0);

    /* If this assertion fails, the inode structure is not exactly
       one sector in size, and you should fix that. */
    ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

    disk_inode = calloc(1, sizeof *disk_inode);
    if (disk_inode != NULL)
    {
        disk_inode->length = length;
        disk_inode->magic = INODE_MAGIC;
        disk_inode->is_dir = false;

        /* Allocate data blocks for the initial length */
        if (length > 0)
        {
            size_t sectors = bytes_to_sectors(length);
            size_t i;
            bool allocation_success = true;

            for (i = 0; i < sectors; i++)
            {
                if (get_data_block(disk_inode, i, true) == (block_sector_t)-1)
                {
                    allocation_success = false;
                    break;
                }
            }

            if (!allocation_success)
            {
                /* Free any blocks we allocated */
                free_inode_blocks(disk_inode);
                free(disk_inode);
                return false;
            }
        }

        /* Write the inode to disk */
        block_write(fs_device, sector, disk_inode);
        success = true;
        free(disk_inode);
    }
    return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *inode_open(block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  lock_acquire(&inode_lock);

  /* Check whether this inode is already open. */
  for (e = list_begin(&open_inodes); e != list_end(&open_inodes); e = list_next(e))
  {
    inode = list_entry(e, struct inode, elem);
    if (inode->sector == sector)
    {
      inode_reopen(inode);
      lock_release(&inode_lock);
      return inode;
    }
  }

  /* Allocate memory. */
  inode = malloc(sizeof *inode);
  if (inode == NULL)
  {
    lock_release(&inode_lock);
    return NULL;
  }

  /* Initialize. */
  list_push_front(&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read(fs_device, inode->sector, &inode->data);
  
  lock_release(&inode_lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *inode_reopen(struct inode *inode)
{
  if (inode != NULL)
  {
    inode->open_cnt++;
  }
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t inode_get_inumber(const struct inode *inode) { return inode->sector; }

/* Returns true if INODE was removed from the directory tree. */
bool inode_is_removed(const struct inode *inode)
{
    return inode != NULL && inode->removed;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void inode_close(struct inode *inode)
{
    /* Ignore null pointer. */
    if (inode == NULL)
    {
        return;
    }

    lock_acquire(&inode_lock);

    /* Release resources if this was the last opener. */
    if (--inode->open_cnt == 0)
    {
        /* Remove from inode list. */
        list_remove(&inode->elem);

        lock_release(&inode_lock);

        /* Deallocate blocks if removed. */
        if (inode->removed)
        {
            free_map_release(inode->sector, 1);
            free_inode_blocks(&inode->data);
        }

        free(inode);
    }
    else
    {
        lock_release(&inode_lock);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void inode_remove(struct inode *inode)
{
  ASSERT(inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at(struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0)
  {
    /* Disk sector to read, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually copy out of this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
    {
      break;
    }

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
    {
      /* Read full sector directly into caller's buffer. */
      block_read(fs_device, sector_idx, buffer + bytes_read);
    }
    else
    {
      /* Read sector into bounce buffer, then partially copy
         into caller's buffer. */
      if (bounce == NULL)
      {
        bounce = malloc(BLOCK_SECTOR_SIZE);
        if (bounce == NULL)
        {
          break;
        }
      }
      block_read(fs_device, sector_idx, bounce);
      memcpy(buffer + bytes_read, bounce + sector_ofs, chunk_size);
    }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_read += chunk_size;
  }
  free(bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   File growth is now supported through dynamic block allocation. */
off_t inode_write_at(struct inode *inode, const void *buffer_, off_t size, off_t offset)
{
    const uint8_t *buffer = buffer_;
    off_t bytes_written = 0;
    uint8_t *bounce = NULL;

    if (inode->deny_write_cnt)
    {
        return 0;
    }

    /* Extend file if writing past EOF */
    off_t write_end = offset + size;
    if (write_end > inode->data.length)
    {
        /* Allocate blocks for the extended portion */
        size_t old_sectors = bytes_to_sectors(inode->data.length);
        size_t new_sectors = bytes_to_sectors(write_end);

        /* Allocate new blocks */
        for (size_t i = old_sectors; i < new_sectors; i++)
        {
            if (get_data_block(&inode->data, i, true) == (block_sector_t)-1)
            {
                /* Allocation failed - return what we've written so far */
                return bytes_written;
            }
        }

        /* Update length and write back inode to disk */
        inode->data.length = write_end;
        block_write(fs_device, inode->sector, &inode->data);
    }

    while (size > 0)
    {
        /* Sector to write, starting byte offset within sector. */
        block_sector_t sector_idx = byte_to_sector(inode, offset);
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;

        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        off_t inode_left = inode_length(inode) - offset;
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
        int min_left = inode_left < sector_left ? inode_left : sector_left;

        /* Number of bytes to actually write into this sector. */
        int chunk_size = size < min_left ? size : min_left;
        if (chunk_size <= 0)
        {
            break;
        }

        if (sector_idx == (block_sector_t)-1)
        {
            /* Block allocation failed during extension */
            break;
        }

        if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
            /* Write full sector directly to disk. */
            block_write(fs_device, sector_idx, buffer + bytes_written);
        }
        else
        {
            /* We need a bounce buffer. */
            if (bounce == NULL)
            {
                bounce = malloc(BLOCK_SECTOR_SIZE);
                if (bounce == NULL)
                {
                    break;
                }
            }

            /* If the sector contains data before or after the chunk
               we're writing, then we need to read in the sector
               first.  Otherwise we start with a sector of all zeros. */
            if (sector_ofs > 0 || chunk_size < sector_left)
            {
                block_read(fs_device, sector_idx, bounce);
            }
            else
            {
                memset(bounce, 0, BLOCK_SECTOR_SIZE);
            }
            memcpy(bounce + sector_ofs, buffer + bytes_written, chunk_size);
            block_write(fs_device, sector_idx, bounce);
        }

        /* Advance. */
        size -= chunk_size;
        offset += chunk_size;
        bytes_written += chunk_size;
    }
    free(bounce);

    return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void inode_deny_write(struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write(struct inode *inode)
{
  ASSERT(inode->deny_write_cnt > 0);
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length(const struct inode *inode) { return inode->data.length; }

/* Creates a directory inode at the given sector.
   Returns true if successful. */
bool inode_create_dir(block_sector_t sector)
{
    struct inode_disk *disk_inode = NULL;
    bool success = false;

    disk_inode = calloc(1, sizeof *disk_inode);
    if (disk_inode != NULL)
    {
        disk_inode->length = 0;
        disk_inode->magic = INODE_MAGIC;
        disk_inode->is_dir = true;

        /* Write the inode to disk */
        block_write(fs_device, sector, disk_inode);
        success = true;
        free(disk_inode);
    }
    return success;
}

/* Returns true if INODE represents a directory. */
bool inode_is_dir(const struct inode *inode)
{
    return inode != NULL && inode->data.is_dir;
}
