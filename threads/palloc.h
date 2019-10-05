#ifndef THREADS_PALLOC_H
#define THREADS_PALLOC_H

#include <stddef.h>
#include <stdint.h>

/* How to allocate pages. */
enum palloc_flags
  {
    PAL_ASSERT = 001,           /* Panic on failure. */
    PAL_ZERO = 002,             /* Zero page contents. */
    PAL_USER = 004              /* User page. */
  };

/* Maximum number of pages to put in user pool. */
extern size_t user_page_limit;

void palloc_init (void);
uint32_t *palloc_get_page (enum palloc_flags);
uint32_t *palloc_get_multiple (enum palloc_flags, size_t page_cnt);
void palloc_free_page (uint32_t *);
void palloc_free_multiple (uint32_t *, size_t page_cnt);

#endif /* threads/palloc.h */
