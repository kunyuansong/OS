#include <slub.h>
#include <pmm.h>
#include <list.h>
#include <string.h>
#include <stdio.h>

/*
 * Two-level allocator:
 *   PMM alloc_pages/free_pages -> 4 KiB slab pages -> small objects.
 * Requests larger than 2048 bytes use dedicated multi-page allocations.
 *
 * Educational simplification: one CPU, no concurrency, no per-CPU cache.
 */
#define SLUB_ALIGNMENT    16
#define SLUB_NCLASSES     8
#define SLUB_SMALL_MAX    2048
#define SLUB_BITMAP_BITS  256
#define SLUB_MAGIC        0x534C55424B4D454DULL

struct slub_cache {
    size_t object_size;
    list_entry_t slabs;
};

struct slub_slab {
    uint64_t magic;
    struct Page *page;             /* First physical page from alloc_pages(). */
    struct slub_cache *cache;      /* NULL means a dedicated large allocation. */
    list_entry_t link;             /* Member of cache->slabs for small objects. */
    void *free_head;               /* Intrusive linked list of free objects. */
    size_t object_size;
    size_t npages;
    unsigned int capacity;
    unsigned int inuse;
    unsigned char used[SLUB_BITMAP_BITS / 8];
};

static const size_t slub_sizes[SLUB_NCLASSES] = {
    16, 32, 64, 128, 256, 512, 1024, 2048,
};
static struct slub_cache slub_caches[SLUB_NCLASSES];
static bool slub_ready;

static size_t slub_align_up(size_t size) {
    return (size + SLUB_ALIGNMENT - 1) & ~(size_t)(SLUB_ALIGNMENT - 1);
}

static size_t slub_data_offset(void) {
    return slub_align_up(sizeof(struct slub_slab));
}

static void *slub_page_kva(struct Page *page) {
    return (void *)(page2pa(page) + va_pa_offset);
}

static bool slub_used(const struct slub_slab *slab, unsigned int idx) {
    return (slab->used[idx / 8] & (1U << (idx % 8))) != 0;
}

static void slub_set_used(struct slub_slab *slab, unsigned int idx, bool value) {
    unsigned char mask = (unsigned char)(1U << (idx % 8));
    if (value) {
        slab->used[idx / 8] |= mask;
    } else {
        slab->used[idx / 8] &= (unsigned char)~mask;
    }
}

void slub_init(void) {
    assert(!slub_ready);
    assert(slub_data_offset() < PGSIZE);
    assert((PGSIZE - slub_data_offset()) / SLUB_ALIGNMENT <= SLUB_BITMAP_BITS);

    for (unsigned int i = 0; i < SLUB_NCLASSES; i++) {
        slub_caches[i].object_size = slub_sizes[i];
        list_init(&slub_caches[i].slabs);
    }
    slub_ready = 1;
}

static struct slub_slab *slub_new_small(struct slub_cache *cache) {
    struct Page *page = alloc_pages(1);
    if (page == NULL) {
        return NULL;
    }

    struct slub_slab *slab = (struct slub_slab *)slub_page_kva(page);
    memset(slab, 0, sizeof(*slab));
    slab->magic = SLUB_MAGIC;
    slab->page = page;
    slab->cache = cache;
    slab->object_size = cache->object_size;
    slab->npages = 1;
    slab->capacity = (PGSIZE - slub_data_offset()) / cache->object_size;
    assert(slab->capacity > 0 && slab->capacity <= SLUB_BITMAP_BITS);
    list_init(&slab->link);

    /* Store the next-free pointer inside each currently unused object. */
    char *data = (char *)slab + slub_data_offset();
    for (unsigned int i = 0; i < slab->capacity; i++) {
        void *obj = data + i * cache->object_size;
        *(void **)obj = slab->free_head;
        slab->free_head = obj;
    }

    list_add(&cache->slabs, &slab->link);
    return slab;
}

static void *slub_alloc_small(struct slub_cache *cache) {
    struct slub_slab *slab = NULL;
    list_entry_t *head = &cache->slabs;

    for (list_entry_t *le = list_next(head); le != head; le = list_next(le)) {
        struct slub_slab *candidate = to_struct(le, struct slub_slab, link);
        if (candidate->free_head != NULL) {
            slab = candidate;
            break;
        }
    }
    if (slab == NULL && (slab = slub_new_small(cache)) == NULL) {
        return NULL;
    }

    void *obj = slab->free_head;
    slab->free_head = *(void **)obj;
    uintptr_t distance = (uintptr_t)obj -
                         ((uintptr_t)slab + slub_data_offset());
    assert(distance % slab->object_size == 0);
    unsigned int idx = (unsigned int)(distance / slab->object_size);
    assert(idx < slab->capacity && !slub_used(slab, idx));
    slub_set_used(slab, idx, 1);
    slab->inuse++;
    return obj;
}

static void *slub_alloc_large(size_t size) {
    size_t offset = slub_data_offset();
    if (size > (size_t)-1 - offset - (PGSIZE - 1)) {
        return NULL; /* Guard against unsigned integer overflow. */
    }
    size_t npages = (size + offset + PGSIZE - 1) / PGSIZE;
    struct Page *page = alloc_pages(npages);
    if (page == NULL) {
        return NULL;
    }

    struct slub_slab *slab = (struct slub_slab *)slub_page_kva(page);
    memset(slab, 0, sizeof(*slab));
    slab->magic = SLUB_MAGIC;
    slab->page = page;
    slab->cache = NULL;
    slab->object_size = size;
    slab->npages = npages;
    slab->capacity = 1;
    slab->inuse = 1;
    return (char *)slab + offset;
}

void *kmalloc(size_t size) {
    assert(slub_ready);
    if (size == 0) {
        return NULL;
    }
    for (unsigned int i = 0; i < SLUB_NCLASSES; i++) {
        if (size <= slub_sizes[i]) {
            return slub_alloc_small(&slub_caches[i]);
        }
    }
    return slub_alloc_large(size);
}

void kfree(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    assert(slub_ready);

    uintptr_t address = (uintptr_t)ptr;
    assert(address >= KERNBASE && address < KERNTOP);
    uintptr_t physical = address - va_pa_offset;
    assert(physical >= DRAM_BASE && PPN(physical) < npage);

    struct slub_slab *slab = (struct slub_slab *)(address & ~(uintptr_t)(PGSIZE - 1));
    assert(slab->magic == SLUB_MAGIC);
    assert(slab->page == pa2page(physical & ~(uintptr_t)(PGSIZE - 1)));

    uintptr_t start = (uintptr_t)slab + slub_data_offset();
    if (slab->cache == NULL) {
        /* A large allocation must be freed using its returned pointer. */
        assert(address == start && slab->inuse == 1);
        size_t npages = slab->npages;
        struct Page *page = slab->page;
        slab->magic = 0;
        free_pages(page, npages);
        return;
    }

    /* Reject interior pointers, invalid blocks and double-free. */
    assert(address >= start);
    size_t distance = address - start;
    assert(slab->object_size > 0 && distance % slab->object_size == 0);
    unsigned int idx = (unsigned int)(distance / slab->object_size);
    assert(idx < slab->capacity && slub_used(slab, idx));
    slub_set_used(slab, idx, 0);
    assert(slab->inuse > 0);
    slab->inuse--;
    *(void **)ptr = slab->free_head;
    slab->free_head = ptr;

    /* Return a completely empty slab page to the physical page allocator. */
    if (slab->inuse == 0) {
        struct Page *page = slab->page;
        list_del_init(&slab->link);
        slab->magic = 0;
        free_pages(page, 1);
    }
}

void slub_check(void) {
    assert(slub_ready);
    size_t before = nr_free_pages();
    assert(kmalloc(0) == NULL);
    kfree(NULL);

    /* Multiple size classes and 16-byte alignment. */
    const size_t sizes[] = {1, 16, 17, 50, 64, 65, 128, 512,
                            1024, 2048, 2049, 4096, 9000};
    void *ptrs[sizeof(sizes) / sizeof(sizes[0])];
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        ptrs[i] = kmalloc(sizes[i]);
        assert(ptrs[i] != NULL);
        assert(((uintptr_t)ptrs[i] & (SLUB_ALIGNMENT - 1)) == 0);
        memset(ptrs[i], (int)(i + 1), sizes[i]);
    }
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        unsigned char *bytes = (unsigned char *)ptrs[i];
        assert(bytes[0] == (unsigned char)(i + 1));
        assert(bytes[sizes[i] - 1] == (unsigned char)(i + 1));
    }
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        kfree(ptrs[i]);
    }
    assert(nr_free_pages() == before);

    /* Reuse of an object from the free list. */
    void *a = kmalloc(50);
    void *b = kmalloc(50);
    assert(a != NULL && b != NULL && a != b);
    kfree(a);
    void *c = kmalloc(50);
    assert(c == a);
    kfree(c);
    kfree(b);
    assert(nr_free_pages() == before);

    /* Fill more than one slab; free in a different order. */
    void *objects[160];
    for (unsigned int i = 0; i < 160; i++) {
        objects[i] = kmalloc(32);
        assert(objects[i] != NULL);
        memset(objects[i], (int)(i & 0xff), 32);
    }
    for (unsigned int i = 0; i < 160; i += 2) {
        kfree(objects[i]);
    }
    for (unsigned int i = 1; i < 160; i += 2) {
        unsigned char *bytes = (unsigned char *)objects[i];
        assert(bytes[0] == (unsigned char)i && bytes[31] == (unsigned char)i);
        kfree(objects[i]);
    }
    assert(nr_free_pages() == before);
    cprintf("slub_check() succeeded!\n");
}
