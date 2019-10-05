#include <bitmap.h>
#include <debug.h>
#include "devices/disk.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "vm/swap.h"

#define SECTOR_CNT (PGSIZE / DISK_SECTOR_SIZE)

/* The swap device */
static struct disk *swap_device;

/* Tracks in-use and free swap slots */
static struct bitmap *swap_table; /* false: Unused, true: Used */

/* Protects swap_table */
static struct lock swap_lock;

/* 
 * Initialize swap_device, swap_table, and swap_lock.
 */
void 
swap_init (void)
{
  size_t swap_disk_pages;

  /* Initialize swap disk struct and swap_lock */
  swap_device = disk_get(1, 1);
  lock_init(&swap_lock);
  
  /* Manage disk in page size */
  swap_disk_pages = (size_t) disk_size(swap_device) / (PGSIZE / DISK_SECTOR_SIZE);
  /* Initialize bitmap used to manage swap disk */
  swap_table = bitmap_create(swap_disk_pages);
  if(swap_table==NULL)
    PANIC("No memory for swap table!");
}

/*
 * Reclaim a frame from swap device.
 * 1. Check that the page has been already evicted. 
 * 2. You will want to evict an already existing frame
 * to make space to read from the disk to cache. 
 * 3. Re-link the new frame with the corresponding supplementary
 * page table entry. 
 * 4. Do NOT create a new supplementary page table entry. Use the 
 * already existing one. 
 * 5. Use helper function read_from_disk in order to read the contents
 * of the disk into the frame. 
 */ 
/* swaps in the contents at index to addr */
static int recall_cnt =0 ;
void 
swap_in (uint8_t *frame, int index)
{
  /* FRAME must be page-aligned */
  ASSERT(!((int)frame & PGMASK));
  
  //printf("Recall Cnt: %d\n", recall_cnt++);

  read_from_disk(frame, index);
  lock_acquire(&swap_lock);
  bitmap_set(swap_table, index, false);
  lock_release(&swap_lock);
}

/* 
 * Evict a frame to swap device. 
 * 1. Choose the frame you want to evict. 
 * (Ex. Least Recently Used policy -> Compare the timestamps when each 
 * frame is last accessed)
 * 2. Evict the frame. Unlink the frame from the supplementray page table entry
 * Remove the frame from the frame table after freeing the frame with
 * pagedir_clear_page. 
 * 3. Do NOT delete the supplementary page table entry. The process
 * should have the illusion that they still have the page allocated to
 * them. 
 * 4. Find a free block to write you data. Use swap table to get track
 * of in-use and free swap slots.
 */

static int evict_cnt = 0;
/* Put frame into swap disk and return its index in swap disk  */
size_t
swap_out (uint8_t *frame)
{
  size_t free_index;
  
  //printf("Evict Cnt: %d\n", evict_cnt++);

  /* Find free space in swap disk */
  lock_acquire(&swap_lock);
  free_index = bitmap_scan_and_flip(swap_table, 0, 1, false);
  lock_release(&swap_lock);
  //printf("Free index: %d\n", free_index);
  write_to_disk(frame, free_index);
  return free_index;
}

void
swap_free(int index)
{
  ASSERT(bitmap_test(swap_table, index));

  bitmap_set(swap_table, index, false);
}

void
swap_destroy()
{
  bitmap_destroy(swap_table);
}

/* 
 * Read data from swap device to frame. 
 * Look at device/disk.c
 */
void read_from_disk (uint8_t *frame, int index)
{
  int cnt;
  for(cnt=0;cnt<SECTOR_CNT;cnt++)
  {
    disk_read(swap_device, index*SECTOR_CNT + cnt, ((uint8_t *)ptov(frame)) + cnt*DISK_SECTOR_SIZE);
  }
}

/* Write data to swap device from frame */
void write_to_disk (uint8_t *frame, int index)
{
  int cnt;
  for(cnt=0;cnt<SECTOR_CNT;cnt++)
  {
    disk_write(swap_device, index*SECTOR_CNT + cnt, ((uint8_t *)ptov(frame)) + cnt*DISK_SECTOR_SIZE);
  }
}

