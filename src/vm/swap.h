#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stddef.h>

void swap_init(void);
size_t swap_out(void *page);
void swap_in(size_t swap_index, void *page);
void swap_free(size_t swap_index);

#endif /* VM_SWAP_H */
