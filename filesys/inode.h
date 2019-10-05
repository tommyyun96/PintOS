#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <list.h>
#include <stdbool.h>
#include "threads/synch.h"
#include "filesys/off_t.h"
#include "devices/disk.h"

enum file_type {REG_F, DIR_F};

/* In-memory inode. */
struct inode 
  {
    struct lock inode_lock;             /* Lock for this inode */
    struct lock extension_lock;         /* Lock for extension */
    struct lock dir_lock;               /* Only for dir */

    struct list_elem elem;              /* Element in inode list. */

    disk_sector_t sector;               /* Sector number of disk location. */
    volatile int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */ 
  };


struct bitmap;

void inode_init (void);
bool inode_create (disk_sector_t, off_t, enum file_type);
struct inode *inode_open (disk_sector_t);
struct inode *inode_reopen (struct inode *);
disk_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);
bool inode_is_dir(const struct inode *);

#endif /* filesys/inode.h */
