#ifndef VM_VM_H
#define VM_VM_H

#include <stdbool.h>
#include "vm/page.h"
#include "threads/thread.h"

/* VM - coordinates frame allocation/deallocation with SPT updates. */

void vm_init(void);
void *vm_load(void *upage);
void *vm_palloc(void *upage, bool writable);
void vm_free(void *kpage);

void vm_munmap(struct thread *t, mapid_t mapping);
mapid_t vm_mmap(struct thread *t, int fd, void *addr);

#endif /* VM_VM_H */
