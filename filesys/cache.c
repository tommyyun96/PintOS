#include <stdbool.h>
#include <string.h>
#include <debug.h>
#include "devices/disk.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "filesys/cache.h"
#include "filesys/filesys.h"

struct cache_entry
{
  disk_sector_t sec_no;   /* sector stored at this cache entry */
  void *paddr;            /* DRAM addr */

  bool valid;             /* do this contain real sector data? */
  bool dirty;             /* need this be written back to disk? */
  bool accessed;          /* used for clock */

  struct lock entry_lock;
  struct condition no_one_cond;
  int readers_cnt;
  int writers_cnt;
};

static struct lock cache_lock;

static struct cache_entry buffer_cache[64];

static int lookup_sector(disk_sector_t sec_no);
static struct cache_entry *get_cache(disk_sector_t sec_no, bool load);
static int find_candidate_index(void);

void cache_init()
{
  int i;
  
  /* Initialize lock to protect buffer_cache */
  lock_init(&cache_lock);

  /* Initialize each cache_entry */
  for(i=0;i<8;i++)
  {
    int j;
    /* one page covers 8 sectors(caches) */
    void *paddr = (void *)palloc_get_page(PAL_ASSERT);
    for(j=0;j<8;j++)
    {
      struct cache_entry *c_e = &buffer_cache[i*8 + j];
      c_e->paddr = paddr + (DISK_SECTOR_SIZE * j);
      c_e->valid = false;
      lock_init(&c_e->entry_lock);
      cond_init(&c_e->no_one_cond);
    }
  }
}

void cache_read(disk_sector_t sec_no, void *buffer, int offset, int length)
{
  struct cache_entry *c_e;
  
  /* Find cache that corresponds to sec_no
   *  validate if returned cache is really a cache of sec_no sector.
   * It may not be always true
   *  because it can be evicted right after get_cache. */
  while(true)
  {
    c_e = get_cache(sec_no, true);
    lock_acquire(&c_e->entry_lock);
    
    if(c_e->sec_no == sec_no)
      break;  /* breaks out of loop holding the lock!!!!! */
    
    lock_release(&c_e->entry_lock);
  }

  /* increment reader cnt */
  c_e->readers_cnt++;
  lock_release(&c_e->entry_lock);
  
  /* read */
  memcpy(buffer, c_e->paddr + offset, length);
  c_e->accessed = true;

  /* decrement reader cnt(if no one is using this cache, signal) */
  lock_acquire(&c_e->entry_lock);
  c_e->readers_cnt--;

  if(c_e->readers_cnt==0 && c_e->writers_cnt==0)
    cond_signal(&c_e->no_one_cond, &c_e->entry_lock);

  lock_release(&c_e->entry_lock);
}

void cache_write(disk_sector_t sec_no, void *buffer, int offset, int length)
{
  struct cache_entry *c_e;
  
  /* Find cache that corresponds to sec_no
   *  validate if returned cache is really a cache of sec_no sector.
   * It may not be always true
   *  because it can be evicted right after get_cache. */
  while(true)
  {
    /* If entire block has to be overwritten, no need to load */
    c_e = get_cache(sec_no, (length!=DISK_SECTOR_SIZE) );
    lock_acquire(&c_e->entry_lock);
    
    if(c_e->sec_no == sec_no)
      break;  /* breaks out of loop holding the lock!!!!! */
    lock_release(&c_e->entry_lock);
  }

  /* increment writer cnt */
  c_e->writers_cnt++;
  lock_release(&c_e->entry_lock);
  
  /* write */
  memcpy(c_e->paddr + offset, buffer, length);
  c_e->accessed = true;
  c_e->dirty = true;

  /* decrement writer cnt(if no one is using this cache, signal) */
  lock_acquire(&c_e->entry_lock);
  c_e->writers_cnt--;

  if(c_e->readers_cnt==0 && c_e->writers_cnt==0)
    cond_signal(&c_e->no_one_cond, &c_e->entry_lock);

  lock_release(&c_e->entry_lock);
}

void cache_zero_block(disk_sector_t sec_no)
{
  struct cache_entry *c_e;
  
  /* Find cache that corresponds to sec_no
   *  validate if returned cache is really a cache of sec_no sector.
   * It may not be always true
   *  because it can be evicted right after get_cache. */
  while(true)
  {
    c_e = get_cache(sec_no, false); /* Whole block will be overwritten with 0's */
    lock_acquire(&c_e->entry_lock);
    
    if(c_e->sec_no == sec_no)
      break;  /* breaks out of loop holding the lock!!!!! */
    lock_release(&c_e->entry_lock);
  }

  /* increment writer cnt */
  c_e->writers_cnt++;
  lock_release(&c_e->entry_lock);
  
  /* write */
  memset(c_e->paddr, 0, DISK_SECTOR_SIZE);
  c_e->accessed = true;
  c_e->dirty = true;

  /* decrement writer cnt(if no one is using this cache, signal) */
  lock_acquire(&c_e->entry_lock);
  c_e->writers_cnt--;

  if(c_e->readers_cnt==0 && c_e->writers_cnt==0)
    cond_signal(&c_e->no_one_cond, &c_e->entry_lock);

  lock_release(&c_e->entry_lock);
}

void cache_flush()
{
  int i;

  /* prevent further cache r/w requests */
  lock_acquire(&cache_lock);

  for(i=0;i<64;i++)
  {
    struct cache_entry *c_e = &buffer_cache[i];
    
    lock_acquire(&c_e->entry_lock);
   
    if(c_e->valid)
    {
      /* wait until all pending r/w requests are done */
      //while(c_e->readers_cnt!=0 || c_e->writers_cnt!=0)
      //  cond_wait(&c_e->no_one_cond, &c_e->entry_lock);
      
      /* if dirty, should write it back to the disk */
      if(c_e->dirty)
        disk_write(filesys_disk, c_e->sec_no, c_e->paddr);

      /* now, disk and cache is coherent. */
      /* make cache entry unoccupied */
      c_e->valid = false;
    }

    lock_release(&c_e->entry_lock);
  }
}

static struct cache_entry *get_cache(disk_sector_t sec_no, bool load)
{
  int cache_index;

  lock_acquire(&cache_lock);
  cache_index = lookup_sector(sec_no);
  
  /* sector not cached */
  if(cache_index == -1)
  {
    int candidate_index = find_candidate_index();
    struct cache_entry *c_e = &buffer_cache[candidate_index];
    lock_acquire(&c_e->entry_lock);
    
    /* already occupied cache, so evict */
    if(c_e->valid)
    {
      disk_sector_t old_sec_no = c_e->sec_no;
      /* wait until there is no reader or writer */
      while(c_e->readers_cnt!=0 || c_e->writers_cnt!=0)
        cond_wait(&c_e->no_one_cond, &c_e->entry_lock);

      /* Here, set this c_e as the cache of 
       *  sec_no sector and release global lock.
       * Then, possible read or write to sec_no sector may find c_e,
       *  and wait on its entry_lock, which will eventually be released 
       *  after eviction and data preparation */
      c_e->sec_no = sec_no;
      lock_release(&cache_lock);
      
      /* Evict: if dirty, should write it back to the disk */
      if(c_e->dirty)
        disk_write(filesys_disk, old_sec_no, c_e->paddr);
    }
    /* c_e is unoccupied cache entry */
    else
    {
      c_e->sec_no = sec_no;
      lock_release(&cache_lock);
      c_e->valid = true;
      c_e->readers_cnt = 0;
      c_e->writers_cnt = 0;
    }
    
    /* Load from disk sector if needed */
    if(load)
      disk_read(filesys_disk, sec_no, c_e->paddr);

    /* now c_e is ready */
    c_e->dirty = false;
    c_e->accessed = true; /* prevent immediate eviction */
    
    lock_release(&c_e->entry_lock);

    return c_e;
  }
  /* sector cached */
  else
  {
    lock_release(&cache_lock);
    return &buffer_cache[cache_index];
  }
}

static int lookup_sector(disk_sector_t sec_no)
{
  bool found = false;
  int i;
  for(i=0;i<64;i++)
  {
    if(buffer_cache[i].valid && buffer_cache[i].sec_no == sec_no)
    {
      found = true;
      break;
    }
  }
  if(found)
    return i;
  else
    return -1;
}

static int clock_index = -1;
static int find_candidate_index()
{
  struct cache_entry *c_e;

  while(true)
  {
    /* move clock */
    clock_index = (clock_index+1) % 64;
    
    c_e = &buffer_cache[clock_index];
    
    /* when cache is invalid just return it */
    if(!c_e->valid)
      break;
    
    /* if cache is not accessed, return it(may be evicted) */
    if(!c_e->accessed)
      break;
    
    c_e->accessed = false;
  }

  return clock_index;
}
