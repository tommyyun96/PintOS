#ifndef USERPROG_PAGEDIR_H
#define USERPROG_PAGEDIR_H

#include <stdbool.h>
#include <stdint.h>

uint32_t *pagedir_create (void);
void pagedir_destroy (uint32_t *pd);
bool pagedir_set_page (uint32_t *pd, uint32_t *upage, uint32_t *kpage, bool rw);
uint32_t *pagedir_get_page (uint32_t *pd, const uint32_t *upage);
void pagedir_clear_page (uint32_t *pd, uint32_t *upage);
bool pagedir_get_writable(uint32_t *pd, const uint32_t *vpage);
void pagedir_set_readonly (uint32_t *pd, const uint32_t *vpage);
bool pagedir_is_dirty (uint32_t *pd, const uint32_t *upage);
void pagedir_set_dirty (uint32_t *pd, const uint32_t *upage, bool dirty);
bool pagedir_is_accessed (uint32_t *pd, const uint32_t *upage);
void pagedir_set_accessed (uint32_t *pd, const uint32_t *upage, bool accessed);
void pagedir_activate (uint32_t *pd);

#endif /* userprog/pagedir.h */
