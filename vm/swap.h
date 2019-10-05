#ifndef VM_SWAP_H
#define VM_SWAP_H

void swap_init (void);
void swap_in (uint8_t *frame, int index);
size_t swap_out (uint8_t *frame);
void swap_free(int index);
void swap_destroy(void);
void read_from_disk (uint8_t *frame, int index);
void write_to_disk (uint8_t *frame, int index);

#endif /* vm/swap.h */
