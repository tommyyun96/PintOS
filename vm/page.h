#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <inttypes.h>
#include <stdbool.h>
#include <hash.h>

/* enum for the type of page */
enum page_type
{
  ELF_RO,
  ELF_W,
  STACK,
  MMAPPED
};

/* enum for current data location  */
enum data_loc
{
  DISK_FILE,  /* fs.dsk */
  DISK_SWAP,  /* swap.dsk */
  FRAME       /* DRAM */
};

struct sup_page_table_entry 
{
  struct hash_elem h_elem;

  enum page_type type;
  enum data_loc loc;
	uint32_t *uaddr;
  bool is_dirty;

  /* type = disk_file */
  int fd;
  int offset;
  int length;
  
  /* type = frame */
  struct frame_table_entry *frame_entry;

  /* type = disk_swap */
  int swap_index;
};

void sup_page_init (void);

bool page_register_elf(uint32_t *uaddr, int fd, int ofs, int length, bool writable);
bool page_register_stack(uint32_t *uaddr);
bool page_register_mmapped(uint32_t *uaddr, int fd, int length);
void page_unregister_mmapped(uint32_t *uaddr, int page_cnt);
void unregister_all_pages(void);

struct sup_page_table_entry *lookup_sup_pte(struct thread *t, uint32_t *uaddr);

void sup_page_table_free(void);

/*struct sup_page_table_entry *allocate_page (void *addr);*/

#endif /* vm/page.h */
