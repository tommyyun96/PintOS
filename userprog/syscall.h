#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>
#include "threads/synch.h"
#include "filesys/file.h"

struct mmap_list_entry
{
  uint32_t *uaddr;    /* user address that the file is mmapped */
  int page_cnt;       /* length of the file */
  int original_fd;    /* Used to avoid multiple mapping */
  int fd;
  int mmap_id;
  struct list_elem elem;
};

void syscall_init (void);

#endif /* userprog/syscall.h */
