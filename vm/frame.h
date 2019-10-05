#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>

struct lock frame_lock;

struct frame_table_entry
{
	uint32_t *frame;  /* physical address of the frame */
	struct thread *owner; /* owner of the frame */
  struct sup_page_table_entry* spte;
  bool busy;        /* prevent undesirable eviction  */

  struct list_elem l_elem;
};

void frame_init (void);
/* allocated frame is initially writable.
 * caller is responsible for making it read-only if needed. */
void allocate_frame (struct sup_page_table_entry *);
void deallocate_frame (struct frame_table_entry *);
void free_allocated_frames(void);
void frame_table_free(void);

#endif /* vm/frame.h */
