#include "filesys/directory.h"

#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#ifdef USERPROG
#include "threads/thread.h"
#endif

#include <list.h>
#include <stdio.h>
#include <string.h>

/* A directory. */
struct dir
{
    struct inode *inode; /* Backing store. */
    off_t pos;           /* Current position. */
};

/* A single directory entry. */
struct dir_entry
{
    block_sector_t inode_sector; /* Sector number of header. */
    char name[NAME_MAX + 1];     /* Null terminated file name. */
    bool in_use;                 /* In use or free? */
};

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool dir_create(block_sector_t sector, size_t entry_cnt)
{
  /* Create the directory inode. */
  if (!inode_create_dir(sector))
  {
    return false;
  }

  /* Open the newly created directory. */
  struct inode *inode = inode_open(sector);
  if (inode == NULL)
  {
    return false;
  }

  struct dir *dir = dir_open(inode);
  if (dir == NULL)
  {
    inode_close(inode);
    return false;
  }

  /* Add "." entry pointing to itself. */
  bool success = dir_add(dir, ".", sector);

  /* Add ".." entry. For root directory, ".." points to itself.
     For other directories, the caller should update ".." after creation. */
  if (success)
  {
    success = dir_add(dir, "..", sector);
  }

  dir_close(dir);
  return success;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *dir_open(struct inode *inode)
{
  struct dir *dir = calloc(1, sizeof *dir);
  if (inode != NULL && dir != NULL)
  {
    dir->inode = inode;
    dir->pos = 0;
    return dir;
  }
  else
  {
    inode_close(inode);
    free(dir);
    return NULL;
  }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *dir_open_root(void) { return dir_open(inode_open(ROOT_DIR_SECTOR)); }

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *dir_reopen(struct dir *dir) { return dir_open(inode_reopen(dir->inode)); }

/* Destroys DIR and frees associated resources. */
void dir_close(struct dir *dir)
{
  if (dir != NULL)
  {
    inode_close(dir->inode);
    free(dir);
  }
}

/* Returns the inode encapsulated by DIR. */
struct inode *dir_get_inode(struct dir *dir) { return dir->inode; }

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool lookup(const struct dir *dir, const char *name, struct dir_entry *ep, off_t *ofsp)
{
  struct dir_entry e;
  size_t ofs;

  ASSERT(dir != NULL);
  ASSERT(name != NULL);

  for (ofs = 0; inode_read_at(dir->inode, &e, sizeof e, ofs) == sizeof e; ofs += sizeof e)
  {
    if (e.in_use && !strcmp(name, e.name))
    {
      if (ep != NULL)
      {
        *ep = e;
      }
      if (ofsp != NULL)
      {
        *ofsp = ofs;
      }
      return true;
    }
  }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool dir_lookup(const struct dir *dir, const char *name, struct inode **inode)
{
  struct dir_entry e;

  ASSERT(dir != NULL);
  ASSERT(name != NULL);

  if (lookup(dir, name, &e, NULL))
  {
    *inode = inode_open(e.inode_sector);
  }
  else
  {
    *inode = NULL;
  }

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool dir_add(struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT(dir != NULL);
  ASSERT(name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen(name) > NAME_MAX)
  {
    return false;
  }

  /* Check that NAME is not in use. */
  if (lookup(dir, name, NULL, NULL))
  {
    goto done;
  }

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.

     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at(dir->inode, &e, sizeof e, ofs) == sizeof e; ofs += sizeof e)
  {
    if (!e.in_use)
    {
      break;
    }
  }

  /* Write slot. */
  e.in_use = true;
  strlcpy(e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at(dir->inode, &e, sizeof e, ofs) == sizeof e;

done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool dir_remove(struct dir *dir, const char *name)
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT(dir != NULL);
  ASSERT(name != NULL);

  /* Prevent removing "." or ".." */
  if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
  {
    return false;
  }

  /* Find directory entry. */
  if (!lookup(dir, name, &e, &ofs))
  {
    goto done;
  }

  /* Open inode. */
  inode = inode_open(e.inode_sector);
  if (inode == NULL)
  {
    goto done;
  }

  /* If it's a directory, check if it's empty (only "." and ".." entries). */
  if (inode_is_dir(inode))
  {
    struct dir *target_dir = dir_open(inode_reopen(inode));
    if (target_dir == NULL)
    {
      goto done;
    }

    char entry_name[NAME_MAX + 1];
    bool has_other_entries = false;
    
    /* Iterate through all entries. */
    while (dir_readdir(target_dir, entry_name))
    {
      /* Skip "." and ".." */
      if (strcmp(entry_name, ".") != 0 && strcmp(entry_name, "..") != 0)
      {
        has_other_entries = true;
        break;
      }
    }

    dir_close(target_dir);

    if (has_other_entries)
    {
      /* Directory is not empty. */
      goto done;
    }
  }

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at(dir->inode, &e, sizeof e, ofs) != sizeof e)
  {
    goto done;
  }

  /* Remove inode. */
  inode_remove(inode);
  success = true;

done:
  inode_close(inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool dir_readdir(struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at(dir->inode, &e, sizeof e, dir->pos) == sizeof e)
  {
    dir->pos += sizeof e;
    if (e.in_use)
    {
      strlcpy(name, e.name, NAME_MAX + 1);
      return true;
    }
  }
  return false;
}

/* Parses PATH and returns the final component name, storing the parent
   directory in *DIR_OUT. Returns NULL on failure. Caller must free the
   returned string. Handles both absolute and relative paths.
   Examples:
   - "/a/b/c" -> dir_out points to "/a/b", returns "c"
   - "a/b" -> dir_out points to cwd/a, returns "b"
   - "a" -> dir_out points to cwd, returns "a"
   - "/" -> dir_out points to root, returns "" (empty string)
   - "." -> dir_out points to cwd, returns "."
   - ".." -> dir_out points to cwd, returns ".." */
char *dir_parse_path(const char *path, struct dir **dir_out)
{
  if (path == NULL || strlen(path) == 0)
  {
    return NULL;
  }

  /* Duplicate the path for modification. */
  char *path_copy = malloc(strlen(path) + 1);
  if (path_copy == NULL)
  {
    return NULL;
  }
  strlcpy(path_copy, path, strlen(path) + 1);

  /* Start from root or current directory. */
  struct dir *dir;
  if (path[0] == '/')
  {
    dir = dir_open_root();
  }
  else
  {
#ifdef USERPROG
    struct thread *cur = thread_current();
    if (cur->cwd == NULL)
    {
      dir = dir_open_root();
    }
    else
    {
      dir = dir_reopen(cur->cwd);
    }
#else
    dir = dir_open_root();
#endif
  }

  if (dir == NULL)
  {
    free(path_copy);
    return NULL;
  }

  /* Tokenize the path and navigate. */
  char *save_ptr;
  char *token;
  char *prev_token = NULL;
  
  token = strtok_r(path_copy, "/", &save_ptr);
  while (token != NULL)
  {
    char *next_token = strtok_r(NULL, "/", &save_ptr);
    
    if (next_token != NULL)
    {
      /* Not the last component, navigate into this directory. */
      if (strcmp(token, ".") == 0)
      {
        /* Stay in current directory. */
      }
      else if (strcmp(token, "..") == 0)
      {
        /* Go to parent directory. */
        struct inode *parent_inode;
        if (dir_lookup(dir, "..", &parent_inode))
        {
          struct dir *parent_dir = dir_open(parent_inode);
          if (parent_dir != NULL)
          {
            dir_close(dir);
            dir = parent_dir;
          }
          else
          {
            inode_close(parent_inode);
          }
        }
      }
      else
      {
        /* Navigate to the named directory. */
        struct inode *next_inode;
        if (dir_lookup(dir, token, &next_inode))
        {
          if (inode_is_dir(next_inode))
          {
            struct dir *next_dir = dir_open(next_inode);
            if (next_dir != NULL)
            {
              dir_close(dir);
              dir = next_dir;
            }
            else
            {
              inode_close(next_inode);
              dir_close(dir);
              free(path_copy);
              return NULL;
            }
          }
          else
          {
            /* Path component is not a directory. */
            inode_close(next_inode);
            dir_close(dir);
            free(path_copy);
            return NULL;
          }
        }
        else
        {
          /* Directory component doesn't exist. */
          dir_close(dir);
          free(path_copy);
          return NULL;
        }
      }
    }
    
    prev_token = token;
    token = next_token;
  }

  /* Return the directory and the final component. */
  *dir_out = dir;
  
  /* Allocate and return the final component name. */
  char *name = NULL;
  if (prev_token != NULL)
  {
    name = malloc(strlen(prev_token) + 1);
    if (name != NULL)
    {
      strlcpy(name, prev_token, strlen(prev_token) + 1);
    }
  }
  else
  {
    /* Path was "/" or empty after tokenization. */
    name = malloc(1);
    if (name != NULL)
    {
      name[0] = '\0';
    }
  }

  free(path_copy);
  return name;
}

/* Looks up PATH and returns the inode for the file/directory.
   Returns true if successful, storing the directory containing the
   file in *DIR_OUT and the filename in NAME_OUT. Returns false on failure.
   NAME_OUT must be a buffer of size NAME_MAX + 1. */
bool dir_lookup_path(const char *path, struct dir **dir_out, char *name_out)
{
  char *name = dir_parse_path(path, dir_out);
  if (name == NULL)
  {
    return false;
  }

  if (strlen(name) > NAME_MAX)
  {
    dir_close(*dir_out);
    free(name);
    return false;
  }

  strlcpy(name_out, name, NAME_MAX + 1);
  free(name);
  return true;
}

/* Updates the ".." entry in the directory at CHILD_SECTOR to point to
   PARENT_SECTOR. Returns true if successful, false on failure. */
bool dir_set_parent(block_sector_t child_sector, block_sector_t parent_sector)
{
  struct inode *child_inode = inode_open(child_sector);
  if (child_inode == NULL)
  {
    return false;
  }

  struct dir *child_dir = dir_open(child_inode);
  if (child_dir == NULL)
  {
    inode_close(child_inode);
    return false;
  }

  /* Find and update the ".." entry. */
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  if (lookup(child_dir, "..", &e, &ofs))
  {
    /* Update the inode_sector field. */
    e.inode_sector = parent_sector;
    success = inode_write_at(child_dir->inode, &e, sizeof e, ofs) == sizeof e;
  }

  dir_close(child_dir);
  return success;
}

/* Sets the position of DIR. */
void dir_set_pos(struct dir *dir, off_t pos)
{
  ASSERT(dir != NULL);
  dir->pos = pos;
}

/* Returns the current position of DIR. */
off_t dir_get_pos(struct dir *dir)
{
  ASSERT(dir != NULL);
  return dir->pos;
}
