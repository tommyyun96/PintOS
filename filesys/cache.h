#include "devices/disk.h"

void cache_init(void);

void cache_read(disk_sector_t sec_no, void *buffer, int offset, int length);

void cache_write(disk_sector_t sec_no, void *buffer, int offset, int length);

void cache_zero_block(disk_sector_t sec_no);

void cache_flush(void);
