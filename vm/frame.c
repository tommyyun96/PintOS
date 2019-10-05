#include <debug.h>
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"

static struct frame_table_entry *evict(void);
static struct frame_table_entry *choose_victim(void);

static struct list frame_table;

static struct list_elem *frame_iterator;
static bool frame_iterator_initialized = false;

/*
 * Initialize frame table
 */
void 
frame_init (void)
{
  list_init(&frame_table);
  lock_init(&frame_lock);
}


/* 
 * Allocate a frame for UPAGE
 * *** Newly allocated frame is WRITABLE!! ***
 */
void
allocate_frame (struct sup_page_table_entry *spte)
{
  uint32_t *kpage;
  struct frame_table_entry *entry;
  struct thread *curr = thread_current();

  lock_acquire(&frame_lock);

  /* Check if uaddr is already mapped */
  if(pagedir_get_page (curr->pagedir, spte->uaddr) !=NULL)
    PANIC("UPAGE already mapped!");

  /* Allocate a frame from user pool */
  kpage = palloc_get_page(PAL_USER);

  /* if alloc failed, evict! */
  if(kpage == NULL)
  {
    entry = evict();
    list_remove(&entry->l_elem);
    /* (Just in case...) */
    if(list_size(&frame_table)==0)
      frame_iterator_initialized = false;
  }
  else
  {
    entry = malloc(sizeof(struct frame_table_entry)); /* ***USE OF MALLOC*** */
    entry->frame = (uint32_t *)vtop(kpage);
  }
  
  /* Now, associate frame with current thread and its uaddr */
  entry->owner = curr;
  entry->spte = spte;
  entry->busy = true;
  spte->frame_entry = entry;
  
  /* map from upage to new frame */
  if(!pagedir_set_page(curr->pagedir, spte->uaddr, ptov(entry->frame), true))
    PANIC("Couldn't map upage to new frame");

  /* Register entry on the frame_table */
  list_push_back(&frame_table, &entry->l_elem);

  lock_release(&frame_lock);

  return;
}

static struct frame_table_entry *
evict()
{
  struct frame_table_entry *victim = choose_victim();

  ASSERT(victim->spte->loc == FRAME);

  victim->spte->is_dirty = victim->spte->is_dirty 
          || pagedir_is_dirty(victim->owner->pagedir, victim->spte->uaddr);
  

  switch(victim->spte->type)
  {
    case ELF_RO:
      victim->spte->loc = DISK_FILE;
      break;
    case ELF_W:
      if(!victim->spte->is_dirty)
      {
        victim->spte->loc = DISK_FILE;
        break;
      }
      /* Note that if ELF_W region is dirty, fall through */
    case STACK:
      {
        int swap_index = swap_out((uint8_t *)victim->frame);
        victim->spte->swap_index = swap_index;
        victim->spte->loc = DISK_SWAP;
        break;
      }
    case MMAPPED:
      {
        int fd = victim->spte->fd;

        int offset = victim->spte->offset;
        int length = victim->spte->length;
        uint32_t *uaddr = victim->spte->uaddr;
        if(victim->spte->is_dirty)
        {
          file_write_at(victim->owner->fd_table[fd], uaddr, length, offset);
          /* Once write back is done, the page is not dirty any more */
          victim->spte->is_dirty = false;
        }
        victim->spte->loc = DISK_FILE;
        break;
      }
  }

  pagedir_clear_page(victim->owner->pagedir, victim->spte->uaddr);

  return victim;
}

/* Choosing a victim uses clock algorithm explained in
 *  https://en.wikipedia.org/wiki/Page_replacement_algorithm */
static struct frame_table_entry *
choose_victim()
{
  ASSERT(list_size(&frame_table)!=0);
  struct frame_table_entry *entry;

  if(!frame_iterator_initialized)
  {
    frame_iterator_initialized = true;
    frame_iterator = list_begin(&frame_table);
  }

  while(true)
  {

    /* If the list_iterator reached the end of the list, take it back to the first beginning */
    if(frame_iterator==list_end(&frame_table))
      frame_iterator = list_begin(&frame_table);

    entry = list_entry(frame_iterator, struct frame_table_entry, l_elem);

    if(! entry->busy)
    {
      /* if the frame is accessed, mark it unaccessed and move on */
      if(pagedir_is_accessed(entry->owner->pagedir, entry->spte->uaddr))
        pagedir_set_accessed(entry->owner->pagedir, entry->spte->uaddr, false);
      else
        break;
    }

    /* Iterate */
    frame_iterator = list_next(frame_iterator);
  }
 
  /* Note that entry will be deleted from the list
   *  so that the frame_iterator has to move on */
  frame_iterator = list_next(frame_iterator); 

  return entry;
}

/*
 * Deallocate Frame *** MUST be called within frame_lock ***
 */
void
deallocate_frame(struct frame_table_entry *entry)
{
  ASSERT(entry->spte->loc==FRAME);
  pagedir_clear_page(entry->owner->pagedir, entry->spte->uaddr);
  
  /* Prevent the frame_iterator from being messed up. */
  if(frame_iterator==&entry->l_elem)
    list_next(frame_iterator);

  list_remove(&entry->l_elem);

  if(list_size(&frame_table)==0)
  {
    frame_iterator_initialized = false;
  }
  
  palloc_free_page(ptov(entry->frame));
  free(entry);
}

/* Cleans up the resources used to manage frame_table */
void frame_table_free()
{
  struct list_elem *e;

  for(e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
  {
    struct frame_table_entry *entry = list_entry(e, struct frame_table_entry, l_elem);
    free(entry);
  }
}

