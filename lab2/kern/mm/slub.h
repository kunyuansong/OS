#ifndef __KERN_MM_SLUB_H__
#define __KERN_MM_SLUB_H__

#include <defs.h>

/* Simplified, single-CPU SLUB-style object allocator. */
void slub_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void slub_check(void);

#endif /* __KERN_MM_SLUB_H__ */
