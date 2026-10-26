#include <buddy_pmm.h>
#include <list.h>
#include <stdio.h>

/* 128 MiB DRAM, 4 KiB/page: at most 32768 pages. */
#define BUDDY_MAX_ORDER 15

static list_entry_t buddy_free_list[BUDDY_MAX_ORDER + 1];
static size_t buddy_nr_free;

static size_t buddy_size(unsigned int order) {
    return (size_t)1 << order;
}

/* Register a free block of 2^order pages. */
static void buddy_add_block(struct Page *page, unsigned int order) {
    size_t sz = buddy_size(order);
    assert(((size_t)(page - pages) & (sz - 1)) == 0);
    page->property = sz;
    SetPageProperty(page);
    list_add(&buddy_free_list[order], &page->page_link);
}

static void buddy_remove_block(struct Page *page) {
    assert(PageProperty(page));
    list_del_init(&page->page_link);
    ClearPageProperty(page);
    page->property = 0;
}

static void buddy_init(void) {
    for (unsigned int order = 0; order <= BUDDY_MAX_ORDER; order++) {
        list_init(&buddy_free_list[order]);
    }
    buddy_nr_free = 0;
}

/* Partition the initial free range into aligned power-of-two blocks. */
static void buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);

    for (struct Page *p = base; p < base + n; p++) {
        assert(PageReserved(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
        list_init(&p->page_link);
    }

    buddy_nr_free += n;

    while (n > 0) {
        size_t index = (size_t)(base - pages);
        unsigned int order = BUDDY_MAX_ORDER;

        while (order > 0 &&
               (buddy_size(order) > n ||
                (index & (buddy_size(order) - 1)) != 0)) {
            order--;
        }

        buddy_add_block(base, order);

        size_t chunk = buddy_size(order);
        base += chunk;
        n -= chunk;
    }
}

/* Allocate the smallest available power-of-two block >= n pages. */
static struct Page *buddy_alloc_pages(size_t n) {
    assert(n > 0);

    if (n > buddy_nr_free) {
        return NULL;
    }

    unsigned int requested = 0;
    while (requested <= BUDDY_MAX_ORDER &&
           buddy_size(requested) < n) {
        requested++;
    }

    if (requested > BUDDY_MAX_ORDER) {
        return NULL;
    }

    unsigned int found = requested;
    while (found <= BUDDY_MAX_ORDER &&
           list_empty(&buddy_free_list[found])) {
        found++;
    }

    if (found > BUDDY_MAX_ORDER) {
        return NULL;
    }

    struct Page *base =
        le2page(list_next(&buddy_free_list[found]), page_link);

    buddy_remove_block(base);

    /* Repeatedly split into two equal halves. */
    while (found > requested) {
        found--;
        struct Page *right = base + buddy_size(found);
        buddy_add_block(right, found);
    }

    /* Remember actual allocated size for correct release. */
    size_t actual = buddy_size(requested);
    base->property = actual;
    buddy_nr_free -= actual;

    return base;
}

/* Return a block and recursively merge with its free buddy. */
static void buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    assert(base >= pages && base < pages + (npage - nbase));

    size_t actual = base->property;
    assert(!PageProperty(base) &&
           actual >= n &&
           actual != 0 &&
           actual <= buddy_size(BUDDY_MAX_ORDER) &&
           (actual & (actual - 1)) == 0);

    size_t index = (size_t)(base - pages);
    assert((index & (actual - 1)) == 0);
    assert(actual <= npage - nbase - index);

    for (struct Page *p = base; p < base + actual; p++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
        list_init(&p->page_link);
    }

    buddy_nr_free += actual;

    unsigned int order = 0;
    while (buddy_size(order) < actual) {
        order++;
    }

    while (order < BUDDY_MAX_ORDER) {
        size_t chunk = buddy_size(order);
        size_t partner_index = index ^ chunk;

        if (partner_index + chunk > npage - nbase) {
            break;
        }

        struct Page *partner = pages + partner_index;

        if (!PageProperty(partner) ||
            partner->property != chunk) {
            break;
        }

        buddy_remove_block(partner);

        if (partner_index < index) {
            index = partner_index;
            base = partner;
        }

        order++;
    }

    buddy_add_block(base, order);
}

static size_t buddy_nr_free_pages(void) {
    return buddy_nr_free;
}

/* Initial smoke test; detailed tests will be added separately. */

/* Validate free-list structure and buddy merging invariants. */
static void buddy_check_invariants(void) {
    size_t total = 0;
    size_t count = 0;
    size_t page_count = npage - nbase;

    for (unsigned int order = 0;
         order <= BUDDY_MAX_ORDER; order++) {

        list_entry_t *head = &buddy_free_list[order];
        list_entry_t *le = list_next(head);
        size_t chunk = buddy_size(order);

        while (le != head) {
            assert(++count <= page_count);

            struct Page *p = le2page(le, page_link);
            assert(p >= pages && p < pages + page_count);

            size_t index = (size_t)(p - pages);

            assert(chunk <= page_count - index);
            assert((index & (chunk - 1)) == 0);
            assert(PageProperty(p));
            assert(p->property == chunk);

            // Two free buddies of equal order must be merged.
            size_t partner_index = index ^ chunk;
            if (partner_index + chunk <= page_count) {
                struct Page *partner = pages + partner_index;
                assert(!PageProperty(partner) ||
                       partner->property != chunk);
            }

            total += chunk;
            assert(total <= page_count);

            le = list_next(le);
        }
    }

    assert(total == buddy_nr_free_pages());
}

static void buddy_check(void) {
    size_t before = buddy_nr_free_pages();
    assert(before >= 32);

    buddy_check_invariants();

    // Test 1: Allocation and power-of-two rounding.
    struct Page *a = buddy_alloc_pages(1);
    struct Page *b = buddy_alloc_pages(2);
    struct Page *c = buddy_alloc_pages(3);
    struct Page *d = buddy_alloc_pages(5);

    assert(a != NULL && b != NULL);
    assert(c != NULL && d != NULL);

    assert(a->property == 1);
    assert(b->property == 2);
    assert(c->property == 4);
    assert(d->property == 8);

    assert(!PageProperty(a));
    assert(!PageProperty(b));
    assert(!PageProperty(c));
    assert(!PageProperty(d));

    assert(((size_t)(b - pages) & 1) == 0);
    assert(((size_t)(c - pages) & 3) == 0);
    assert(((size_t)(d - pages) & 7) == 0);

    assert(buddy_nr_free_pages() == before - 15);
    buddy_check_invariants();

    // Test 2: Insufficient memory.
    assert(buddy_alloc_pages(before + 1) == NULL);

    // Test 3: Release in a different order.
    buddy_free_pages(b, 2);
    buddy_free_pages(d, 5);
    buddy_free_pages(a, 1);
    buddy_free_pages(c, 3);

    assert(buddy_nr_free_pages() == before);
    buddy_check_invariants();

    // Test 4: Fragmentation and buddy coalescing.
    struct Page *p[16];

    for (size_t i = 0; i < 16; i++) {
        p[i] = buddy_alloc_pages(1);
        assert(p[i] != NULL);

        for (size_t j = 0; j < i; j++) {
            assert(p[i] != p[j]);
        }
    }

    assert(buddy_nr_free_pages() == before - 16);
    buddy_check_invariants();

    // Free alternating pages first.
    for (size_t i = 0; i < 16; i += 2) {
        buddy_free_pages(p[i], 1);
    }

    assert(buddy_nr_free_pages() == before - 8);
    buddy_check_invariants();

    // Free remaining pages to allow buddy merging.
    for (size_t i = 1; i < 16; i += 2) {
        buddy_free_pages(p[i], 1);
    }

    assert(buddy_nr_free_pages() == before);
    buddy_check_invariants();

    cprintf("buddy_check: detailed tests passed!\n");
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
