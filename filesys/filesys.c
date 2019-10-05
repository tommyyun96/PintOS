#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "threads/thread.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "devices/disk.h"

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

static struct lock filesys_lock;

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  filesys_disk = disk_get (0, 1);
  if (filesys_disk == NULL)
    PANIC ("hd0:1 (hdb) not present, file system initialization failed");
  

  lock_init(&filesys_lock);

  inode_init ();
  cache_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();

  /* When file system initialization is done, set cwd of the main thread */
  thread_current()->cwd = dir_open_root();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
  cache_flush();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *path, off_t initial_size, enum file_type type) 
{
  struct dir *dir;
  char name[16];
  disk_sector_t inode_sector = 0;
  bool success = false;
  
  if( !path_parse(path, &dir, name) )
    return false;

  if( !free_map_allocate(1, &inode_sector) )
  {
    dir_close(dir);
    return false;
  }

  if( type==DIR_F )
  {
    if( !dir_create (inode_sector, dir_get_inode(dir)->sector) )
      goto done;
  }
  else
  {
    if( !inode_create(inode_sector, initial_size, REG_F) )
      goto done;
  }

  if( !dir_add(dir, name, inode_sector) )
    goto done;

  success = true;

  done:
  if(!success)
    free_map_release(inode_sector, 1);  
  dir_close(dir);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *path)
{
  struct inode *inode = NULL;
  struct dir *dir;
  char name[16];
  
  if( !path_parse(path, &dir, name) )
    return NULL;

  if (dir != NULL)
  {
    dir_lookup (dir, name, &inode);
  }

  /* Since inode is opened within dir_lookup, in which
   *  dir_lock is acquired, there is no need to worry about 
   *  inode being removed. */

  dir_close(dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *path) 
{
  struct dir *dir;
  char name[16];
  bool success = false;
  if( !path_parse(path, &dir, name) )
    return false;
  success = dir_remove(dir, name);
  dir_close(dir);
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  cache_zero_block(0);
  cache_zero_block(1);
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, ROOT_DIR_SECTOR))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
