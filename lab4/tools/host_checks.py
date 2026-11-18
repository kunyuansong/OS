#!/usr/bin/env python3
"""Run actual lab4 C logic with host mocks; does not emulate RISC-V hardware."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
COMMON = r'''
#define __LIBS_ATOMIC_H__
#include <defs.h>
#include <mmu.h>
#include <proc.h>
#include <error.h>
#include <string.h>
extern int printf(const char *, ...);
extern void exit(int) __attribute__((noreturn));
static int checks;
#define assert(x) do { checks++; if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define panic(...) do { printf("unexpected panic line %d\n", __LINE__); exit(1); } while (0)
#define static_assert(x) _Static_assert((x), #x)
void *memset(void *v, char c, size_t n) { char *p=v; while(n--) *p++=c; return v; }
void *memcpy(void *d, const void *s, size_t n) { char *p=d; const char *q=s; while(n--) *p++=*q++; return d; }
char *strncpy(char *d, const char *s, size_t n) { char *p=d; while(n--) { *p++=*s; if(*s) s++; } return d; }
int cprintf(const char *s, ...) { return 0; }
'''
PMM = r'''
static struct Page pool[16];
static int used[16], alloc_fail, pte_fail, releases, invalidations;
static pte_t table[16];
static pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create) {
    if(pte_fail || la/PGSIZE >= 16) return NULL;
    return &table[la/PGSIZE];
}
static struct Page *alloc_page(void) {
    if(alloc_fail) return NULL;
    for(int i=0;i<16;i++) if(!used[i]) { used[i]=1; pool[i].ref=0; return &pool[i]; }
    return NULL;
}
static void free_page(struct Page *p) { int i=p-pool; assert(i>=0 && i<16 && used[i]); assert(p->ref==0); used[i]=0; releases++; }
static int page_ref_inc(struct Page *p) { return ++p->ref; }
static int page_ref_dec(struct Page *p) { return --p->ref; }
static uintptr_t page2ppn(struct Page *p) { return p-pool+1; }
static struct Page *pte2page(pte_t p) { return pool+(p>>10)-1; }
static pte_t pte_create(uintptr_t ppn, int perm) { return (ppn<<10)|perm|PTE_V; }
static void tlb_invalidate(pde_t *pgdir, uintptr_t la) { invalidations++; }
'''
PMM_MAIN = r'''
int main(void) {
    pde_t root[512]={0};
    struct Page *p=alloc_page(), *q=alloc_page();
    assert(page_insert(root,p,0,0)==0 && p->ref==1 && (table[0]&PTE_R));
    assert(page_insert(root,p,PGSIZE,PTE_W|PTE_U)==0 && p->ref==2);
    assert((table[1]&(PTE_R|PTE_W|PTE_U|PTE_A|PTE_D))==(PTE_R|PTE_W|PTE_U|PTE_A|PTE_D));
    int before=releases;
    assert(page_insert(root,p,PGSIZE,PTE_X)==0 && p->ref==2 && releases==before);
    assert((table[1]&(PTE_R|PTE_W|PTE_U|PTE_X|PTE_D))==PTE_X);
    assert(page_insert(root,q,0,PTE_R)==0 && p->ref==1 && q->ref==1);
    page_remove(root,PGSIZE); assert(p->ref==0 && releases==before+1 && table[1]==0);
    page_remove(root,PGSIZE); assert(releases==before+1);
    page_remove(root,0); assert(q->ref==0 && table[0]==0);
    p=alloc_page(); pte_fail=1; before=invalidations;
    assert(page_insert(root,p,0,PTE_W)==-E_NO_MEM && p->ref==0 && invalidations==before);
    before=releases; assert(pgdir_alloc_page(root,0,0)==NULL && releases==before+1);
    free_page(p); pte_fail=0; alloc_fail=1;
    assert(pgdir_alloc_page(root,0,0)==NULL); alloc_fail=0;
    p=pgdir_alloc_page(root,0,PTE_W); assert(p && p->ref==1);
    page_remove(root,0); assert(p->ref==0);
    pte_fail=1; before=invalidations; page_remove(root,0); assert(invalidations==before);
    printf("PMM host checks: %d assertions PASS\n",checks);
    return 0;
}
'''
WALKER_EXTRA = r'''
static int budget=-1;
static char physical[16][PGSIZE] __attribute__((aligned(4096)));
static void *kaddr(uintptr_t pa) { return physical[(pa>>PGSHIFT)-1]; }
#define KADDR(pa) kaddr(pa)
#define pde2page(pde) pte2page(pde)
static void set_page_ref(struct Page *p, int ref) { p->ref=ref; }
static uintptr_t page2pa(struct Page *p) { return page2ppn(p)<<PGSHIFT; }
static pte_t ptd_create(uintptr_t ppn) { return pte_create(ppn,PTE_V); }
'''
WALKER_MAIN = r'''
static int allocated(void) { int n=0; for(int i=0;i<16;i++) n+=used[i]; return n; }
int main(void) {
    pde_t root[512]={0};
    assert(get_pte(root,0,0)==NULL && allocated()==0);
    struct Page *p=alloc_page();
    budget=0; assert(page_insert(root,p,0,0)==-E_NO_MEM && root[0]==0 && allocated()==1 && p->ref==0);
    budget=1; assert(page_insert(root,p,0,0)==-E_NO_MEM && root[0]==0 && allocated()==1 && p->ref==0);
    budget=-1; assert(page_insert(root,p,0,PTE_W|PTE_U)==0 && allocated()==3);
    assert((root[0]&0x3ff)==PTE_V);
    pde_t *mid=KADDR(PDE_ADDR(root[0])); assert((mid[0]&0x3ff)==PTE_V);
    pte_t *leaf=NULL; assert(get_page(root,0,&leaf)==p && leaf && (*leaf&PTE_U));
    assert(get_page(root,PGSIZE,NULL)==NULL && allocated()==3);
    assert(page_insert(root,p,PGSIZE,0)==0 && allocated()==3 && p->ref==2);
    page_remove(root,0); page_remove(root,PGSIZE); assert(allocated()==2 && p->ref==0);
    root[1]=pte_create(1,PTE_R|PTE_X); assert(get_pte(root,1UL<<PDX1SHIFT,1)==NULL && allocated()==2);
    root[1]=0; mid[1]=pte_create(1,PTE_R|PTE_X); assert(get_pte(root,1UL<<PDX0SHIFT,1)==NULL && allocated()==2);
    printf("Sv39 walker host checks: %d assertions PASS\n",checks);
    return 0;
}
'''
PROC = r'''
#define CLONE_VM 0x100
#define SSTATUS_SIE 2
#define SSTATUS_SPIE 0x20
#define SSTATUS_SPP 0x100
static uintptr_t status=SSTATUS_SIE, last_satp;
static int irq_enabled=1, switches, flushes, fail_kmalloc, fail_stack, live_pcbs, live_stacks;
#define read_csr(reg) status
#define write_csr(reg,val) (last_satp=(val))
#define local_intr_save(f) do { (f)=irq_enabled; irq_enabled=0; } while(0)
#define local_intr_restore(f) do { if(f) irq_enabled=1; } while(0)
static void flush_tlb(void) { flushes++; }
uintptr_t boot_pgdir_pa=0x80400000;
char bootstack[KSTACKSIZE];
static union { uintptr_t align; char data[1024]; } pcbs[32];
static int pcb_used[32], stack_used[32];
static struct Page stack_pages[32];
static char stacks[32][KSTACKSIZE] __attribute__((aligned(16)));
void *kmalloc(size_t n) {
    if(fail_kmalloc) return NULL;
    assert(n<=sizeof(pcbs[0]));
    for(int i=0;i<32;i++) if(!pcb_used[i]) { pcb_used[i]=1; live_pcbs++; return pcbs[i].data; }
    return NULL;
}
void kfree(void *p) {
    for(int i=0;i<32;i++) if(p==pcbs[i].data) { assert(pcb_used[i]); pcb_used[i]=0; live_pcbs--; return; }
    assert(0);
}
struct Page *alloc_pages(size_t n) {
    assert(n==KSTACKPAGE);
    if(fail_stack) return NULL;
    for(int i=0;i<32;i++) if(!stack_used[i]) { stack_used[i]=1; live_stacks++; return &stack_pages[i]; }
    return NULL;
}
void free_pages(struct Page *p, size_t n) { int i=p-stack_pages; assert(n==KSTACKPAGE && stack_used[i]); stack_used[i]=0; live_stacks--; }
void *page2kva(struct Page *p) { return stacks[p-stack_pages]; }
struct Page *kva2page(void *p) { for(int i=0;i<32;i++) if(p==stacks[i]) return &stack_pages[i]; assert(0); return NULL; }
void wakeup_proc(struct proc_struct *proc);
void schedule(void);
uint32_t hash32(uint32_t val, unsigned int bits) { return val & ((1U<<bits)-1); }
'''
PROC_MAIN = r'''
void kernel_thread_entry(void) {}
void forkrets(struct trapframe *tf) { assert(0); }
void switch_to(struct context *from, struct context *to) { assert(!irq_enabled); assert(from!=to); switches++; }
static int child(void *arg) { return 0; }
static int list_count(void) { int n=0; for(list_entry_t *le=list_next(&proc_list);le!=&proc_list;le=list_next(le)) n++; return n; }
int main(void) {
    proc_init();
    assert(current==idleproc && idleproc->pid==0 && idleproc->kstack==(uintptr_t)bootstack);
    assert(idleproc->parent==NULL && idleproc->mm==NULL && idleproc->tf==NULL && idleproc->runs==0 && idleproc->context.ra==0);
    assert(initproc->pid==1 && initproc->parent==idleproc && initproc->mm==NULL);
    assert(initproc->pgdir==boot_pgdir_pa && initproc->context.ra==(uintptr_t)forkret);
    assert(initproc->tf->gpr.sp==initproc->kstack+KSTACKSIZE && initproc->context.sp==(uintptr_t)initproc->tf);
    assert(initproc->tf->gpr.a0==0 && (initproc->tf->status&(SSTATUS_SIE|SSTATUS_SPIE|SSTATUS_SPP))==(SSTATUS_SPIE|SSTATUS_SPP));
    assert(initproc->tf->epc==(uintptr_t)kernel_thread_entry && initproc->name[4]==0);
    assert(nr_process==2 && list_count()==1 && live_pcbs==2 && live_stacks==1);
    fail_kmalloc=1; assert(kernel_thread(child,NULL,0)==-E_NO_MEM); fail_kmalloc=0;
    assert(live_pcbs==2 && live_stacks==1 && list_count()==1);
    fail_stack=1; assert(kernel_thread(child,NULL,0)==-E_NO_MEM); fail_stack=0;
    assert(live_pcbs==2 && live_stacks==1 && nr_process==2 && list_count()==1);
    nr_process=MAX_PROCESS; assert(kernel_thread(child,NULL,0)==-E_NO_FREE_PROC); nr_process=2;
    int pid=kernel_thread(child,(void*)1234,0); struct proc_struct *p=find_proc(pid);
    assert(pid==2 && p && p->tf->gpr.s0==(uintptr_t)child && p->tf->gpr.s1==1234);
    assert(p->state==PROC_RUNNABLE && p->parent==idleproc && list_count()==2);
    schedule(); assert(current==initproc && switches==1 && flushes==1 && irq_enabled);
    assert(last_satp==(((uintptr_t)8<<60)|(boot_pgdir_pa>>PGSHIFT)));
    schedule(); assert(current==p && switches==2);
    schedule(); assert(current==initproc && switches==3);
    p->state=PROC_SLEEPING; current=initproc; int s=switches; schedule(); assert(current==initproc && switches==s);
    initproc->state=PROC_SLEEPING; schedule(); assert(current==idleproc);
    s=switches; schedule(); assert(current==idleproc && switches==s);
    p->state=PROC_RUNNABLE; irq_enabled=0; proc_run(p); assert(!irq_enabled && current==p);
    printf("Process/scheduler host checks: %d assertions PASS\n",checks);
    return 0;
}
'''
ALLOCATOR = r'''
static void set_bit(int n, volatile void *p) { *(volatile uint_t *)p |= (uint_t)1<<n; }
static void clear_bit(int n, volatile void *p) { *(volatile uint_t *)p &= ~((uint_t)1<<n); }
static bool test_bit(int n, volatile void *p) { return (*(volatile uint_t *)p>>n)&1; }
extern const struct pmm_manager default_pmm_manager;
static struct Page arena_pages[64];
static size_t npage=64;
static int page_ref(struct Page *p) { return p->ref; }
static void set_page_ref(struct Page *p,int n) { p->ref=n; }
static uintptr_t page2pa(struct Page *p) { return (p-arena_pages)*PGSIZE; }
static struct Page *alloc_pages(size_t n) { return default_pmm_manager.alloc_pages(n); }
static void free_pages(struct Page *p,size_t n) { default_pmm_manager.free_pages(p,n); }
static size_t nr_free_pages(void) { return default_pmm_manager.nr_free_pages(); }
#define alloc_page() alloc_pages(1)
#define free_page(p) free_pages(p,1)
'''
ALLOCATOR_MAIN = r'''
int main(void) {
    default_pmm_manager.init();
    for(int i=0;i<64;i++) SetPageReserved(&arena_pages[i]);
    default_pmm_manager.init_memmap(arena_pages,64);
    assert(nr_free_pages()==64);
    default_pmm_manager.check();
    assert(nr_free_pages()==64);
    struct Page *p=alloc_pages(64); assert(p==arena_pages && nr_free_pages()==0);
    free_pages(p+20,20); free_pages(p,20); free_pages(p+40,24);
    assert(nr_free_pages()==64);
    assert(alloc_pages(64)==arena_pages);
    printf("Inherited best-fit allocator checks: %d assertions PASS\n",checks);
    return 0;
}
'''

def strip_includes(s):
    return re.sub(r'^#include[^\n]*\n', '', s, flags=re.M)

def main():
    source=(ROOT/'kern/mm/pmm.c').read_text()
    pmm=source[source.index('//LAB4 function1'):source.index('// invalidate a TLB entry')]
    walker=source[source.index('pte_t *get_pte('):source.index('//LAB4 function1')]
    walker_mocks=re.sub(r'static pte_t \*get_pte.*?\n}', '', PMM, flags=re.S)
    walker_mocks=walker_mocks.replace('static struct Page *alloc_page(void)', 'static int budget=-1;\nstatic struct Page *alloc_page(void)')
    walker_mocks=walker_mocks.replace('if(alloc_fail) return NULL;', 'if(alloc_fail || budget==0) return NULL; if(budget>0) budget--;')
    walker_extra=WALKER_EXTRA.replace('static int budget=-1;','')
    proc=strip_includes((ROOT/'kern/process/proc.c').read_text())
    sched=strip_includes((ROOT/'kern/schedule/sched.c').read_text())
    header=(ROOT/'kern/mm/pmm.h').read_text()
    manager=header[header.index('struct pmm_manager\n'):header.index('extern const struct pmm_manager *pmm_manager;')]
    allocator=manager+ALLOCATOR+strip_includes((ROOT/'kern/mm/default_pmm.c').read_text())+ALLOCATOR_MAIN
    with tempfile.TemporaryDirectory(prefix='lab4-host-') as d:
        for name,src in [('allocator',COMMON+allocator), ('pmm',COMMON+PMM+pmm+PMM_MAIN), ('walker',COMMON+walker_mocks+walker_extra+walker+pmm+WALKER_MAIN), ('process',COMMON+PROC+proc+sched+PROC_MAIN)]:
            c=Path(d)/(name+'.c'); exe=Path(d)/name; c.write_text(src)
            cmd=['clang','-std=gnu11','-O1','-fno-builtin','-D__riscv_xlen=64','-Werror','-Wall','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-but-set-variable']
            cmd += ['-I'+str(ROOT/p) for p in ['libs','kern/mm','kern/trap','kern/process']]
            subprocess.run(cmd+[str(c),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
    print('Host mocks do not validate satp, trap restore, or actual context switching.')

if __name__=='__main__':
    main()
