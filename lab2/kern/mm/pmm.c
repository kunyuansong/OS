#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <riscv.h>
#include <dtb.h>
#include <buddy_pmm.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    pmm_manager = &best_fit_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    return pmm_manager->alloc_pages(n);
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    pmm_manager->free_pages(base, n);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
}

//lab2 function1 YOUR CODE



static void page_init(void) {
    extern char end[];
    uint64_t mem_base = get_memory_base();
    uint64_t mem_size = get_memory_size();

    va_pa_offset = PHYSICAL_MEMORY_OFFSET;
    if (mem_base == 0 || mem_size == 0) {
        panic("page_init: missing physical memory information");
    }
    if (mem_size > (uint64_t)-1 - mem_base) {
        panic("page_init: physical memory range overflows");
    }
    uint64_t mem_end = mem_base + mem_size;
    if (mem_base < DRAM_BASE || mem_base > KERNEL_BEGIN_PADDR ||
        mem_end <= KERNEL_BEGIN_PADDR) {
        panic("page_init: physical memory range incompatible with kernel layout");
    }
    cprintf("Physical memory: [0x%016lx, 0x%016lx), size 0x%lx bytes\n",
            mem_base, mem_end, mem_size);

    // Only complete pages within the kernel's direct mapping are manageable.
    uintptr_t managed_end = ROUNDDOWN(
        mem_end < PHYSICAL_MEMORY_END ? mem_end : PHYSICAL_MEMORY_END,
        PGSIZE);
    if ((uintptr_t)end < KERNBASE || (uintptr_t)end >= KERNTOP) {
        panic("page_init: kernel end outside direct mapping");
    }
    uintptr_t kernel_end_pa = PADDR(end);
    uintptr_t pages_pa = ROUNDUP(kernel_end_pa, PGSIZE);
    if (pages_pa >= managed_end) {
        panic("page_init: insufficient memory after kernel image");
    }

    npage = PPN(managed_end);
    size_t page_count = npage - nbase;
    // Check capacity before multiplying or writing any descriptors.
    if (page_count > (managed_end - pages_pa) / sizeof(struct Page)) {
        panic("page_init: insufficient memory for Page descriptors");
    }
    uintptr_t descriptors_end = pages_pa + page_count * sizeof(struct Page);
    uintptr_t free_start = ROUNDUP(descriptors_end, PGSIZE);
    if (free_start >= managed_end) {
        panic("page_init: no complete free physical page remains");
    }

    pages = (struct Page *)(pages_pa + va_pa_offset);
    for (size_t i = 0; i < page_count; i++) {
        struct Page *page = &pages[i];
        set_page_ref(page, 0);
        page->flags = 0;
        page->property = 0;
        list_init(&page->page_link);
        SetPageReserved(page);
    }

    // Firmware, kernel, descriptor storage and alignment padding stay reserved.
    size_t free_count = (managed_end - free_start) / PGSIZE;
    init_memmap(pa2page(free_start), free_count);
    cprintf("PMM free memory: [0x%016lx, 0x%016lx), %lu pages\n",
            free_start, managed_end, (unsigned long)free_count);
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
