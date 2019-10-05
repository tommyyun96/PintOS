#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include <stdio.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* MACROS for inode access */
#define L0_POS (int)offsetof(struct inode_disk, l0blcks)
#define L1_POS (int)offsetof(struct inode_disk, l1blck)
#define L2_POS (int)offsetof(struct inode_disk, l2blck)

/* On-disk inode.
   Must be exactly DISK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    enum file_type type;
    off_t length;
    disk_sector_t l0blcks[64];          /* Direct */
    disk_sector_t l1blck;               /* Indirect */
    disk_sector_t l2blck;               /* Doubly indirect */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[59];               /* Not used. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

/* Returns the disk sector that contains byte offset POS within
   INODE.
   !!! Needs external synchronization on inode sector content !!! */
static disk_sector_t
get_sector_no(disk_sector_t inode_sec_no, off_t pos)
{
  off_t logical_sec_no = pos >> 9; /* same as dividing by 512 */
  
  /* Level 0: Direct */
  if ( logical_sec_no < 64 )
  {
    disk_sector_t data_sec_no;
    int data_sec_no_pos = L0_POS + 4 * logical_sec_no;

    /* read data_sec_no */
    cache_read(inode_sec_no, &data_sec_no, data_sec_no_pos, 4);
    
    /* if data sector is unallocated, prepare with zeros */
    if(data_sec_no==0)
    {
      if(!free_map_allocate(1, &data_sec_no))
        return -1;
      cache_write(inode_sec_no, &data_sec_no, data_sec_no_pos, 4);
      cache_zero_block(data_sec_no);
    }
    
    return data_sec_no; 
  }
  /* Level 1: Indirect */
  else if ( logical_sec_no < 64 + 128 )
  {
    disk_sector_t data_sec_no;
    disk_sector_t l1_blck_sec_no;
    int data_sec_no_pos;

    /* read l1_blck_sec_no */
    cache_read(inode_sec_no, &l1_blck_sec_no, L1_POS, 4);
    
    /* if l1 blck is unallocated, prepare with zeros */
    if(l1_blck_sec_no==0)
    {
      if(!free_map_allocate(1, &l1_blck_sec_no))
        return -1;
      cache_write(inode_sec_no, &l1_blck_sec_no, L1_POS, 4);
      cache_zero_block(l1_blck_sec_no);
    }

    /* read data_sec_no from l1 */
    data_sec_no_pos = 4 * (logical_sec_no - 64);
    cache_read(l1_blck_sec_no, &data_sec_no, data_sec_no_pos, 4);

    /* if data sector is unallocated, prepare with zeros */
    if(data_sec_no==0)
    {
      if(!free_map_allocate(1, &data_sec_no))
      {
        free_map_release(l1_blck_sec_no, 1);
        return -1;
      }
      cache_write(l1_blck_sec_no, &data_sec_no, data_sec_no_pos, 4);
      cache_zero_block(data_sec_no);
    }

    return data_sec_no;
  }
  /* Level 2: Doubly-indirect */
  else if ( logical_sec_no < 64 + 128 + 128*128 )
  {
    disk_sector_t data_sec_no;
    disk_sector_t l2_1_blck_sec_no;
    disk_sector_t l2_2_blck_sec_no;
    int l2_2_blck_sec_no_pos;
    int data_sec_no_pos;

    /* read l2_1_blck_sec_no */
    cache_read(inode_sec_no, &l2_1_blck_sec_no, L2_POS, 4);
    
    /* if l2_1 blck is unallocated, prepare with zeros */
    if(l2_1_blck_sec_no==0)
    {
      if(!free_map_allocate(1, &l2_1_blck_sec_no))
        return -1;
      cache_write(inode_sec_no, &l2_1_blck_sec_no, L2_POS, 4);
      cache_zero_block(l2_1_blck_sec_no);
    }

    /* read l2_2_blck_sec_no from l2_1 */
    l2_2_blck_sec_no_pos = 4 * ( (logical_sec_no - 64 - 128) >> 7 );
    cache_read(l2_1_blck_sec_no, &l2_2_blck_sec_no, l2_2_blck_sec_no_pos, 4);

    /* if l2_2 block sector is unallocated, prepare with zeros */
    if(l2_2_blck_sec_no==0)
    {
      if(!free_map_allocate(1, &l2_2_blck_sec_no))
      {
        free_map_release(l2_1_blck_sec_no, 1);
        return -1;
      }
      cache_write(l2_1_blck_sec_no, &l2_2_blck_sec_no, l2_2_blck_sec_no_pos, 4);
      cache_zero_block(l2_2_blck_sec_no);
    }

    /* read data_sec_no from l2_2_blck */
    data_sec_no_pos = 4 * ( (logical_sec_no - 64 - 128) & (128 - 1) );
    cache_read(l2_2_blck_sec_no, &data_sec_no, data_sec_no_pos, 4);

    /* if data sector is unallocated, prepare with zeros */
    if(data_sec_no==0)
    {
      if(!free_map_allocate(1, &data_sec_no))
      {
        free_map_release(l2_1_blck_sec_no, 1);
        free_map_release(l2_2_blck_sec_no, 1);
        return -1;
      }
      cache_write(l2_2_blck_sec_no, &data_sec_no, data_sec_no_pos, 4);
      cache_zero_block(data_sec_no);
    }

    return data_sec_no;
  }
  else
  {
    PANIC("FILE SIZE LIMIT EXCEEDED");
  }

  PANIC("SHOULDN'T REACH HERE");

  return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;
static struct lock inode_open_close_lock;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  lock_init (&inode_open_close_lock);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   disk.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length, enum file_type type)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  
  if (disk_inode != NULL)
  {
    disk_inode->type = type;
    disk_inode->length = length;
    disk_inode->magic = INODE_MAGIC;
    
    /* write inode to the disk */
    cache_write(sector, disk_inode, 0, DISK_SECTOR_SIZE);
    
    success = true;
    
    free (disk_inode); 
  }

  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector) 
{
  struct list_elem *e;
  struct inode *inode;

  lock_acquire(&inode_open_close_lock);

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          lock_release(&inode_open_close_lock);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
  {
    lock_release(&inode_open_close_lock);
    return NULL;
  }

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  lock_init(&inode->inode_lock);
  lock_init(&inode->extension_lock);
  lock_init(&inode->dir_lock);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_release(&inode_open_close_lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

static inline void
check_and_release_block (disk_sector_t sec_no, int pos)
{
  int data_sec_no;
  cache_read(sec_no, &data_sec_no, pos, 4);
  /* if this block is allocated, free it */
  if(data_sec_no != 0)
    free_map_release(data_sec_no, 1);
}

static void
free_data_blocks (disk_sector_t inode_sec_no)
{
  int i, j;
  int i_pos, j_pos;
  int l1_sec_no, l2_1_sec_no, l2_2_sec_no;
  /* Free L0 */
  for(i=0; i<64; i++)
  {
    i_pos = L0_POS + (4 * i);
    check_and_release_block (inode_sec_no, i_pos);
  }
  /* Free L1 */
  cache_read(inode_sec_no, &l1_sec_no, L1_POS, 4);
    /* If there are some data in level 1, free those */
  if(l1_sec_no!=0)
  {
    for(i=0;i<128;i++)
    {
      i_pos = 4 * i;
      check_and_release_block (l1_sec_no, i_pos);
    }
  }
  /* Free L2 */
  cache_read(inode_sec_no, &l2_1_sec_no, L2_POS, 4);
    /* If there are some data in level 2, free those */
  if(l2_1_sec_no!=0)
  {
    for(i=0;i<128;i++)
    {
      i_pos = 4 * i;
      cache_read(l2_1_sec_no, &l2_2_sec_no, i_pos, 4);
      
      if(l2_2_sec_no!=0)
      {
        for(j=0;j<128;j++)
        {
          j_pos = 4 * j;
          check_and_release_block(l2_2_sec_no, j_pos);
        }
      }
    }
  }
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  lock_acquire(&inode_open_close_lock);

  /* Release resources if this was the last opener. */
  barrier();
  inode->open_cnt--;
  barrier();
  if (inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          /* Deallocate data blocks */
          free_data_blocks(inode->sector);
          free_map_release (inode->sector, 1);
        }

      free (inode); 
    }
  barrier();
  lock_release(&inode_open_close_lock);
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  int initial_inode_length = inode_length(inode);

  while (size > 0) 
    {
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = initial_inode_length - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /* Disk sector to read, starting byte offset within sector. */
      lock_acquire(&inode->inode_lock);
      disk_sector_t sector_idx = get_sector_no(inode->sector, offset);
      lock_release(&inode->inode_lock);
      if(sector_idx == -1)
        break;
 
      cache_read(sector_idx, buffer + bytes_read, sector_ofs, chunk_size);
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs. */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  bool extension = false;
  int extended_length;

  if (inode->deny_write_cnt)
    return 0;

  /* Check if extension is needed and acquire lock */
  if(inode_length(inode) < offset + size)
  {
    lock_acquire(&inode->extension_lock);
    
    barrier();    /* Prevent compiler optimization */

    /* Double check because another process may have already done extension */
    if(inode_length(inode) < offset + size)
    {
      extended_length = offset + size;
      extension = true;
    }
    else
      lock_release(&inode->extension_lock);
  }


  while (size > 0) 
    {
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < sector_left ? size : sector_left;
      if (chunk_size <= 0)
        break;

      /* Sector to write, starting byte offset within sector. */
      lock_acquire(&inode->inode_lock);
      disk_sector_t sector_idx = get_sector_no(inode->sector, offset);
      lock_release(&inode->inode_lock);
      if(sector_idx == -1)
        break;
      cache_write(sector_idx, buffer + bytes_written, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  if(extension)
  {
    /* Update length */
    ASSERT(inode_length(inode) < extended_length);
    cache_write(inode->sector, &extended_length, offsetof(struct inode_disk, length), 4);
    lock_release(&inode->extension_lock);
  }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  int length;
  cache_read(inode->sector, &length, offsetof(struct inode_disk, length), 4);
  return length;
}

/* Returns true if INODE is of DIR_F type */
bool
inode_is_dir (const struct inode *inode)
{
  int type_pos = offsetof(struct inode_disk, type);
  enum file_type type;
  
  /* read type of inode */
  cache_read(inode->sector, &type, type_pos, 4);

  return type == DIR_F;
}
