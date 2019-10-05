#include <debug.h>
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/frame.h"


static unsigned hash (const struct hash_elem *e, void *aux);
static bool less (const struct hash_elem *a, const struct hash_elem *b, void *aux);
static void sup_page_table_entry_free(struct hash_elem *e, void *aux);

/*
 * Initialize supplementary page table
 */
void 
sup_page_init (void)
{
  hash_init(&thread_current()->sup_page_table, hash, less, NULL);
}

/* Register supplementary page table for UADDR.
 * UADDR is mapped to FD. (range: OFS ~ OFS+LENGTH) */
bool
page_register_elf(uint32_t *uaddr, int fd, int ofs, int length, bool writable)
{
  struct sup_page_table_entry *entry = malloc(sizeof(struct sup_page_table_entry));
  
  /* if memory allocation failed, return false */
  if(!entry)
    return false;
  
  /* fill up entry corresponding to args */
  entry->type = writable? ELF_W : ELF_RO;
  entry->loc = DISK_FILE;
  entry->uaddr = uaddr;
  entry->is_dirty = false;
  entry->fd = fd;
  entry->offset = ofs; 
  entry->length = length;
  entry->swap_index = -1; /* -1 means never swapped */
  
  /* add entry to supplementary page table */
  hash_insert(&thread_current()->sup_page_table, &entry->h_elem);

  return true;
}

bool
page_register_stack(uint32_t *uaddr)
{
  struct sup_page_table_entry *entry = malloc(sizeof(struct sup_page_table_entry));
  
  /* If memory allocation failed, return false */
  if(!entry)
    return false;
  
  /* fill up entry corresponding to args */
  entry->type = STACK;
  entry->loc = FRAME;
  entry->uaddr = uaddr;
  entry->is_dirty = false;    /* actually, this is not needed */

  /* Note that the stack do not need to be demand-paging */
  allocate_frame(entry);
  
  thread_current()->stack_bottom = uaddr;

  entry->frame_entry->busy = false;

  /* add entry to supplementary page table */
  hash_insert(&thread_current()->sup_page_table, &entry->h_elem);

  return true;
}

bool
page_register_mmapped(uint32_t *uaddr, int fd, int length)
{
  int page_cnt = (length+(PGSIZE-1))/PGSIZE;
  int i;
  struct sup_page_table_entry *entry;

  for(i=0;i<page_cnt-1;i++)
  {
    entry = malloc(sizeof(struct sup_page_table_entry));
    if(!entry)
      return false;
    
    /* fill in entry */
    entry->type = MMAPPED;
    entry->loc = DISK_FILE;
    entry->uaddr = ((void *)uaddr) + (PGSIZE*i);
    entry->is_dirty = false;
    entry->fd = fd;
    entry->offset = PGSIZE*i;
    entry->length = PGSIZE;
    
    /* add entry to supplementary page table */
    hash_insert(&thread_current()->sup_page_table, &entry->h_elem);
  }
  
  /* entry for the last page(in which length < PGSIZE may be true) */
  entry = malloc(sizeof(struct sup_page_table_entry));
  if(!entry)
    return false;
  
  /* fill in entry */
  entry->type = MMAPPED;
  entry->loc = DISK_FILE;
  entry->uaddr = ((void *)uaddr) + (PGSIZE*i);
  entry->is_dirty = false;
  entry->fd = fd;
  entry->offset = PGSIZE*i;
  entry->length = length - PGSIZE*i;
  
  /* add entry to supplementary page table */
  hash_insert(&thread_current()->sup_page_table, &entry->h_elem);

  return true;
}

void
page_unregister_mmapped(uint32_t *uaddr, int page_cnt)
{
  struct thread *curr = thread_current();
  struct sup_page_table_entry *entry;
  int i;
 
  for(i=0;i<page_cnt;i++)
  {
    entry = lookup_sup_pte(curr, ((void *)uaddr) + (PGSIZE*i));
    
    ASSERT(entry->type==MMAPPED);

    /* Write back to file if dirty */
    if(entry->is_dirty || pagedir_is_dirty(curr->pagedir, entry->uaddr))
      file_write_at(curr->fd_table[entry->fd], entry->uaddr, entry->length, entry->offset);
  
      
    /* Free up memory resources used by entry if any */
    lock_acquire(&frame_lock);
    /* Delete from pagedir */
    pagedir_clear_page(curr->pagedir, entry->uaddr);
    if(entry->loc==DISK_SWAP)
      swap_free(entry->swap_index);
    else if(entry->loc==FRAME)
      deallocate_frame(entry->frame_entry);
    lock_release(&frame_lock);


    /* Remove from sup_page_table and free */
    hash_delete(&curr->sup_page_table, &entry->h_elem);
    free(entry);
  }
}

void
unregister_all_pages()
{
  struct thread *curr = thread_current();
  struct hash_iterator sup_pt_iterator;
  struct sup_page_table_entry *entry;

  lock_acquire(&frame_lock);

  /* Setup sup_pt_iterator */
  hash_first(&sup_pt_iterator, &curr->sup_page_table);

  while(hash_next(&sup_pt_iterator))
  {
    entry = hash_entry(hash_cur(&sup_pt_iterator), struct sup_page_table_entry, h_elem);

    if(entry->loc==FRAME)
      deallocate_frame(entry->frame_entry);
    else if(entry->loc==DISK_SWAP)
      swap_free(entry->swap_index);
  }

  lock_release(&frame_lock);
}

/* Look up supplementary page table entry corresponding to UADDR */
struct sup_page_table_entry *lookup_sup_pte(struct thread *t, uint32_t *uaddr)
{
  struct sup_page_table_entry filter;
  struct hash_elem *target;

  filter.uaddr = (uint32_t *) ((uint32_t)uaddr & ~PGMASK);
  target =  hash_find(&t->sup_page_table, &filter.h_elem);
  
  if(target==NULL)
    return NULL;
  
  return hash_entry(target, struct sup_page_table_entry, h_elem);
}

/* Cleans up the resources used by supplementary page table */
void
sup_page_table_free()
{
  hash_destroy(&thread_current()->sup_page_table, sup_page_table_entry_free);
}

static unsigned
hash (const struct hash_elem *e, void *aux UNUSED)
{
  struct sup_page_table_entry *entry = hash_entry(e, struct sup_page_table_entry, h_elem);
  return ((unsigned)entry->uaddr)>>12;
}

static bool
less (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  struct sup_page_table_entry *entry_a, *entry_b;
  entry_a = hash_entry(a, struct sup_page_table_entry, h_elem);
  entry_b = hash_entry(b, struct sup_page_table_entry, h_elem);
  return entry_a->uaddr < entry_b->uaddr;
}

static void
sup_page_table_entry_free(struct hash_elem *e, void *aux UNUSED)
{
  struct sup_page_table_entry *entry = hash_entry(e, struct sup_page_table_entry, h_elem);
  free(entry);
}
