## 第一部分 页表与内存映射

### 提示词 1 实现和审查页表管理

```text
[PROMPT]
任务：完成当前工程 kern/mm/pmm.c 的 LAB4 function1，并检查 get_pte、pmm_init 与 tools/kernel.ld 的配套逻辑，实现 Sv39 下的 4 KiB 页映射及内核分段预映射。
操作要求：必须直接修改项目中的真实文件。已有正确实现应保留；只修改本模块及必要的内存布局和链接配置。保留 LAB4 function1 注释，不添加个人学号。不要引入缺页分配、换页、用户进程或新的内存分配算法。
输出要求：先阅读实际文件确认接口，完成修改后进行可用的检查，说明修改原因、验证命令与未验证部分。不要修改 tools/test_lab4.c、basic_check 或 best_fit_check 来迎合结果。

[RELY]
可信上下文来自 kern/mm/pmm.h、mmu.h、memlayout.h、pmm.c 和 tools/kernel.ld。
本工程的 pte_t、pde_t、uintptr_t 均为 64 位；Page 含 ref、flags、property、page_link 等字段，描述符大小须使用 sizeof(struct Page)。
PDX1、PDX0、PTX 为三级索引，页偏移使用 PGOFF，PGSIZE=4096，PGSHIFT=12。
PTE_V/R/W/X/U/A/D 已定义；pte_create(ppn,type) 编码物理页号；ptd_create(ppn) 构造非叶子项。
已有 alloc_page、free_page、page_ref_inc、page_ref_dec、set_page_ref、page2ppn、pte2page、pde2page、KADDR、PADDR 和 tlb_invalidate。
boot_pgdir_va 是根页表虚拟指针；boot_pgdir_pa 是物理字节地址；npage 是物理地址上界除以页大小，不是从 DRAM_BASE 开始的页数。
物理页描述符数组下标为 PPN(pa)-nbase，其中 nbase=DRAM_BASE/PGSIZE。
KERNBASE、PHYSICAL_MEMORY_OFFSET、KERNEL_BEGIN_PADDR、PHYSICAL_MEMORY_END 以当前 memlayout.h 为准。
启动汇编使用临时 1 GiB 映射。内核新页表不应就地把该大页当下级页表遍历。

[GUARANTEE]
必须实现或保持正确的接口：
static void page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *ptep);
void page_remove(pde_t *pgdir, uintptr_t la);
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm);
struct Page *pgdir_alloc_page(pde_t *pgdir, uintptr_t la, uint32_t perm);
pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create);
struct Page *get_page(pde_t *pgdir, uintptr_t la, pte_t **ptep_store);
void pmm_init(void);
辅助函数可设为 static，不改变公开接口。

[SPECIFICATION]
page_remove_pte
Pre-Condition：ptep 指向已定位的最终一级页表项，传入的页表和物理页元数据有效。
Post-Condition：有效映射被撤销，原物理页引用减少一次，零引用页面归还分配器，旧转换失效；中间页表不在这里回收。
Case 1：原映射有效，完成上述撤销。
Case 2：原映射无效，不改变引用计数和空闲页数量。

page_remove
Pre-Condition：pgdir 是有效根页表，la 属于本模块支持的 4 KiB 映射范围。
Post-Condition：la 的映射被撤销，或在映射不存在时保持原状。
Requirements：查找时不分配页表；重复移除不得重复释放物理页。

page_insert
Pre-Condition：page 是分配器已分配的有效物理页，pgdir 有效；本次操作由单核内核串行执行。
Post-Condition：成功返回 0，目标页及权限生效，各物理页引用数等于其受管理的有效映射数量；失败返回 -E_NO_MEM，不增加目标页引用。
Case 1：原来没有映射，建立映射并增加一次引用。
Case 2：原来映射不同物理页，减少旧页引用，必要时释放旧页，并建立新映射。
Case 3：原来映射同一物理页，更新权限但保持引用数与该页分配状态。
Case 4：页表分配失败，保留已有映射并回收本次尚未使用的新页表资源。
Requirements：修改映射后刷新 TLB。当前实验中 perm=0 表示只读叶子；写权限必须包含读权限；执行权限可以单独存在；A/D 与当前预映射策略一致。

pgdir_alloc_page
Pre-Condition：pgdir 有效，目标地址用于新的数据页映射。
Post-Condition：成功返回已映射且持有一个映射引用的页面；失败返回 NULL。
Case 1：物理页分配失败，不改变映射。
Case 2：物理页成功但映射失败，归还新分配的数据页，不泄漏页面。

get_pte
Pre-Condition：pgdir 为有效的三级页表根。
Post-Condition：返回最终一级页表项的内核虚拟地址，或返回 NULL。
Case 1：需要的中间页表均存在，查找不消耗页面。
Case 2：中间页表缺失且 create 为假，返回 NULL，不分配页面。
Case 3：允许创建且资源充足，创建并清零中间页表，引用数为 1，返回最终项。
Case 4：资源不足，回收本次新分配但未完成使用的中间页表，不破坏已有页表。
Case 5：遇到本模块不支持的已有大页叶子，返回 NULL，不将其数据页解释成页表。
Requirements：非叶子项不得设置 U、A、D、R、W、X。

get_page
Pre-Condition：页表和地址满足 get_pte 的查找约束。
Post-Condition：有效映射返回对应物理页，否则返回 NULL；提供 ptep_store 时写回查找所得项指针，缺失时为 NULL；不分配资源。

pmm_init
Pre-Condition：启动映射可用，设备树已解析，内核镜像与启动栈位于高地址空间。
Post-Condition：物理页分配器已初始化，内核使用新的 4 KiB 三级页表；text 为 RX，rodata 为 R，data/BSS/剩余受管理内存为 RW，以上均不可由用户态直接访问。
Requirements：不同权限段的边界必须按页对齐；切换 satp 前确保当前代码、栈和全部页表可访问；切换后刷新 TLB；维持有效的启动入口顺序。不得照搬旧式 x86 页表的中间级 U 位规则。
```

### 提示词 2 定位内存与启动故障

```text
[PROMPT]
任务：定位 lab4 启动早期无输出、page_init/check_pgdir/check_boot_pgdir 断言失败或页访问异常。必须依据我附在末尾的完整日志和当前工程修改真实文件。
操作要求：先定位第一次异常，区分工具链、OpenSBI 装载、分页过渡、分配器和页表问题。仅修改导致本次故障的代码，不删除断言或伪造 PASS。不填写个人学号。
输出要求：给出根因、最小修复、验证命令以及仍需检查的项目；缺少日志时说明具体缺少什么证据。

[RELY]
读取 README.md、RUN_IN_VM.md、kern/init/entry.S、tools/kernel.ld、kern/mm/pmm.c、pmm.h、memlayout.h、default_pmm.c 和 Makefile。
boot_pgdir_pa 保存物理字节地址；satp 的 MODE=8，PPN 为物理地址右移 12 位。启动时存在临时恒等映射，高地址跳转后撤销。
本工程最终使用 4 KiB 三级页表；非叶子项只有 V，不带 U/A/D/R/W/X。代码、只读数据和可写数据的权限不同。
以下诊断数据由我附在提示词末尾：实际命令、GCC/QEMU 版本、第一条错误或完整 trapframe。

[GUARANTEE]
保持 page_init、get_pte、page_insert、page_remove、pmm_init 及相关公开接口兼容；仅在证据指向时修改对应模块。
保留原 best-fit 检查函数和 tools/test_lab4.c。

[SPECIFICATION]
Pre-Condition：已经获取实际失败命令与第一次失败位置；若无法运行虚拟机，明确记录验证限制。
Post-Condition：已定位的问题得到修复，已有正确映射、资源回收和内核段权限不被破坏。
Case 1：内核还未进入 kern_init，核对镜像第一条指令、链接入口、OpenSBI 下一跳和 satp 开启后的过渡取指。
Case 2：出现页表断言或访问异常，核对三级索引、地址位宽、非叶子保留位、叶子权限、物理地址与虚拟地址转换以及 TLB。
Case 3：空闲页数不守恒，检查中间页表分配回滚、同页重映射和引用计数；区分保留的页表页与真正泄漏。
Requirements：把预期输出和实际日志分开；只在实际复测后报告通过。
```

## 第二部分 内核线程创建

### 提示词 3 实现和审查创建流程

```text
[PROMPT]
任务：完成 kern/process/proc.c 的 LAB4 function2，建立从 PCB 分配、线程资源初始化到 idle/init 创建的完整内核线程创建流程。
操作要求：必须修改实际文件，保留已有 PID 分配、哈希表、内核栈分配和 forkret 等函数。保持函数签名及 LAB4 function2 注释，不添加学号。不要加入用户空间复制、系统调用、进程退出回收或抢占。
输出要求：实现成功路径与失败回收路径，执行当前环境可用的检查，列出对 PCB、trapframe、内核栈和全局链表的影响。

[RELY]
以 proc.h、trap.h、pmm.h、kmalloc.h、proc.c 和 kern/process/entry.S 的真实定义为准。
proc_struct 包含 state、pid、runs、kstack、need_resched、parent、mm、context、tf、pgdir、flags、name、list_link、hash_link。
context 顺序为 ra、sp、s0 至 s11；trapframe 包含 gpr、status、epc、badvaddr、cause。
KSTACKPAGE=2，KSTACKSIZE=KSTACKPAGE*PGSIZE；PROC_NAME_LEN=15；MAX_PROCESS=4096，MAX_PID=MAX_PROCESS*2。
已有 kmalloc/kfree、setup_kstack/put_kstack、get_pid、hash_proc、find_proc、forkret、wakeup_proc 和 local_intr_save/restore。
boot_pgdir_pa 是物理地址，bootstack 是已有启动栈，CLONE_VM 在 pmm.h 中定义。
kernel_thread_entry 从 s0 获取入口、从 s1 获取参数，入口返回后调用原 do_exit。
所有线程均共享内核地址空间，current->mm 为 NULL。原 do_exit 的 process exit!! panic 是 lab4 骨架保留行为。

[GUARANTEE]
static struct proc_struct *alloc_proc(void);
static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);
void proc_init(void);
可添加模块内部 static 辅助函数；不改变公开接口。

[SPECIFICATION]
alloc_proc
Pre-Condition：堆分配器已经初始化。
Post-Condition：成功返回已初始化 PCB，state=PROC_UNINIT，pid=-1，pgdir 为内核根表物理地址，计数、上下文、名称、指针和标志为初始值，链表节点可安全使用；失败返回 NULL。
Requirements：不得把新分配 PCB 的残留字节作为字段初值。

copy_mm
Pre-Condition：当前只创建内核线程，current->mm 为 NULL，proc 尚未对调度器可见。
Post-Condition：proc->mm 为 NULL，共享内核页表，不分配、复制或释放用户地址空间，返回 0。

copy_thread
Pre-Condition：新线程独立内核栈已分配，tf 指向有效模板。
Post-Condition：在新栈上保存独立中断帧，子线程 a0=0；context 的 ra 指向 forkret，context 的 sp 指向该帧。
Case 1：esp 为零，恢复后的线程 sp 指向其内核栈顶。
Case 2：esp 非零，使用调用者提供的栈指针。
Requirements：帧完整位于已分配栈内，满足 RV64 调用所需对齐，不保留对 kernel_thread 局部模板的悬空引用。

kernel_thread
Pre-Condition：fn 与 arg 是有效入口和参数，current 已初始化。
Post-Condition：成功返回新线程 PID；新线程首次恢复进入 kernel_thread_entry，入口与参数分别位于 s0/s1；失败返回创建错误码。
Requirements：模板初始清零；SPP 和 SPIE 为 1，SIE 为 0；不直接在父线程中调用 fn。

do_fork
Pre-Condition：current 有效，tf 为有效模板，仅涉及内核线程。
Post-Condition：成功返回唯一 PID，新线程有独立 PCB 和内核栈，parent=current，已进入链表和哈希表，state=PROC_RUNNABLE；失败时不发布半初始化线程。
Case 1：达到 MAX_PROCESS，返回 -E_NO_FREE_PROC，不分配资源。
Case 2：PCB 或内核栈分配失败，返回 -E_NO_MEM，释放已取得资源，进程数量及已有链表不变。
Case 3：内存管理准备失败，保留相应错误码并回收此前资源。
Case 4：全部资源初始化成功，在受中断保护的发布阶段分配 PID、加入链表/哈希表、更新数量并置为可运行。
Requirements：新线程按创建顺序加入进程链表；未完成初始化前不得被调度。

proc_init
Pre-Condition：页表、堆分配器可用，进程系统尚未初始化。
Post-Condition：进程链表和 PID 哈希桶初始化；current=idleproc；idle 的 pid=0、状态为 PROC_RUNNABLE、使用 bootstack、need_resched=1；init 的 pid=1、parent=idle、名称为 init，入口为 init_main，参数为 Hello world!!。
Requirements：idle 继承已有执行现场，初始 tf 为 NULL、runs 为 0；idle 不进入普通进程链表；不能静默忽略必要线程创建失败。
```

### 提示词 4 定位线程创建故障

```text
[PROMPT]
任务：修复 lab4 的 Process Create 评分失败、init 字段异常或创建失败后的资源泄漏。请检查我附在末尾的实际日志，并在项目中修改真实文件。
操作要求：以当前 proc.h/trap.h 为准，保留四种进程状态、内核线程共享地址空间和原评分测试，不填学号，不实现后续用户进程功能。
输出要求：说明失败的字段或资源不变量，给出最小修改和验证结果。

[RELY]
阅读 proc.c、proc.h、trap.h、pmm.h、kmalloc.h 和 tools/test_lab4.c 中的 test_lab4_process_create。
idle 使用 bootstack，pid=0，parent/mm/tf 为 NULL；init 的 pid=1，parent=idle，mm=NULL，context.ra/sp 非零。
proc->pgdir 保存物理字节地址；新帧应存放在新线程的两页内核栈内。

[GUARANTEE]
保持 alloc_proc、copy_mm、copy_thread、kernel_thread、do_fork、proc_init 的现有签名；按故障证据修复它们及必要的内部辅助函数。

[SPECIFICATION]
Pre-Condition：给出失败断言或初次异常现场，以及对应构建命令。
Post-Condition：创建成功后字段满足约定，创建失败后 PCB、内核栈、全局进程数量和链表保持一致。
Case 1：PCB 初值错误，核对完整初始化、物理页表地址、字符串和节点状态。
Case 2：init 创建失败，检查 PID 分配、哈希发布顺序、栈分配以及负错误码。
Case 3：内存不足后空闲资源减少，按资源取得顺序检查失败回收，不能释放父线程或启动栈。
Case 4：首次执行前崩溃，核对新帧是否仍引用父线程局部变量、帧位置、sp 对齐、a0/s0/s1、epc 和 status。
Requirements：保留原始失败日志；未实际运行评分时不得声称 Process Create PASS。
```

## 第三部分 线程切换与调度

### 提示词 5 实现和审查切换与调度

```text
[PROMPT]
任务：完成 proc.c、kern/process/switch.S、kern/schedule/sched.c 的 LAB4 function3，串起 idle 让出 CPU、选择可运行线程、切换上下文和首次执行的完整路径。
操作要求：必须修改真实文件。保留现有函数签名、四种状态和 LAB4 function3 注释，暂不填学号。只实现单核协作式轮转，不增加时钟抢占或其他调度算法，不改写原 do_exit。
输出要求：检查 C 与汇编数据布局，运行可用检查，说明切换前后的 current、satp、寄存器、中断和调度计数变化。

[RELY]
proc.h 的 struct context 按顺序存放 ra、sp、s0、s1、s2、s3、s4、s5、s6、s7、s8、s9、s10、s11，每项为 uintptr_t。
RISC-V 汇编参数 a0/from、a1/to 分别指向出入上下文；riscv.h 定义 LOAD、STORE、REGBYTES。
proc_list 只包含普通线程，idleproc 不在链表中；current 为当前线程；need_resched 表示需要重新调度；runs 是调度选中次数。
已有 list_next、le2proc、local_intr_save/restore、write_csr、flush_tlb 和 wakeup_proc。
proc->pgdir 为页表根的物理字节地址。satp 使用 Sv39 MODE=8 和该物理地址右移 12 位后的页号。
首次调度路径为 switch_to -> forkret -> forkrets -> __trapret -> sret -> kernel_thread_entry。

[GUARANTEE]
void switch_to(struct context *from, struct context *to);
void proc_run(struct proc_struct *proc);
void cpu_idle(void);  // proc.h 中声明为 noreturn
void schedule(void);
保持 wakeup_proc(struct proc_struct *proc) 的既有状态约束。
仅允许必要的 static 辅助逻辑，不改变公开结构布局。

[SPECIFICATION]
switch_to
Pre-Condition：from/to 指向有效上下文，调用前已屏蔽中断；目标上下文描述可恢复线程。
Post-Condition：保存旧线程 ra、sp、s0 至 s11，恢复目标线程同一组寄存器，目标线程沿恢复的 ra 继续。
Requirements：字段偏移与 struct context 严格对应，不破坏用于寻址的 a0/a1，不混淆 context 和 trapframe；本阶段不保存浮点寄存器。

proc_run
Pre-Condition：current 与 proc 有效，目标线程可运行，目标页表映射切换路径及内核栈。
Post-Condition：目标与 current 相同时不切换；否则以受中断保护的方式更新 current、切换 Sv39 页表、刷新 TLB 并恢复目标上下文。
Requirements：原线程未来被恢复时返回其原调用链，恢复其此次切换前的中断状态。不得将页表虚拟指针直接写入 satp。

cpu_idle
Pre-Condition：idleproc/current 已建立，启动初始化完成。
Post-Condition：持续执行，不返回；发现 need_resched 时触发调度。
Requirements：不另行创建线程或改动进程状态，不在本函数中添加用户空间执行。

schedule
Pre-Condition：进程链表有效，current 存在；在单核环境调用。
Post-Condition：清除当前 need_resched；选出一个可运行线程或 idle，增加被选中线程 runs；仅当目标与当前不同才切换。
Case 1：从 idle 调度，从普通线程链表头开始选取第一个可运行线程。
Case 2：从普通线程调度，从其后继开始循环扫描，跳过链表哨兵和非 PROC_RUNNABLE 线程，必要时可再次选中当前线程。
Case 3：没有普通线程可运行，选择 idle；空链表也应正确处理。
Requirements：扫描必须终止；选择与切换期间屏蔽中断；不使阻塞或僵尸线程运行，不因为只有当前线程可运行而无意义切栈。
```

### 提示词 6 定位首次切换与 Schedule 评分故障

```text
[PROMPT]
任务：定位并修复 lab4 在首次 switch_to/forkret/sret 后崩溃，或者 Schedule 评分失败的问题。在真实工程中改文件。
操作要求：先确定最后成功到达的断点与第一次异常，保留评分测试和原 do_exit，不添加学号，不用新增抢占、修改测试标志或打印 PASS 规避问题。
输出要求：列出出错的恢复环节、最小补丁及复测命令。当前环境不能运行 QEMU 时，明确给出虚拟机中要检查的寄存器与字段。

[RELY]
读取 proc.c、proc.h、switch.S、sched.c、kern/trap/trapentry.S、trap.h、kern/process/entry.S 和 tools/test_lab4.c。
首次路径为 switch_to -> forkret -> forkrets -> __trapret -> sret -> kernel_thread_entry -> init_main。
context 含 ra/sp/s0..s11；trapframe 有 36 个寄存器宽度，gpr.sp 在第 2 个槽，status/epc 在第 32/33 个槽。
RESTORE_ALL 必须先恢复 CSR 和其余寄存器，最后恢复 sp。线程入口从 s0 取函数，从 s1 取参数。
评分注入的两个测试线程主动调用 schedule，不依赖时钟抢占。init 返回后的 process exit!! 属于原骨架退出路径，但此前的异常或缺少输出不算通过。

[GUARANTEE]
保持 switch_to、proc_run、cpu_idle、schedule、copy_thread、forkret、forkrets 和 kernel_thread_entry 的接口兼容，仅修改证据指向的恢复或调度逻辑。
保留 tools/test_lab4.c 与测试线程行为。

[SPECIFICATION]
Pre-Condition：附上首次异常 trapframe 或 GDB 断点/寄存器记录，以及实际 make 命令。
Post-Condition：线程按照自身保存的上下文继续执行；调度不会永久遗漏可运行线程；中断状态和内核栈有效；原测试能够在真实执行中观察两个测试线程运行。
Case 1：切换前即失败，核对目标状态、current 更新和页表物理地址。
Case 2：到达 forkret 但 sret 后失败，核对 trapframe 地址、epc、SPP/SPIE/SIE、最终恢复 sp 的时机和函数入口参数。
Case 3：首次运行成功但第二次切换失败，核对全部 14 个 context 寄存器、各自栈、返回地址以及嵌套中断保护的恢复。
Case 4：不崩溃但测试线程未交替运行，核对扫描起点、哨兵跳过、循环终止和线程状态；不引入时钟抢占掩盖轮转错误。
Requirements：在虚拟机实际运行 make grade 后才能报告 Schedule PASS 或 30/30；只拿到预处理或 C 语法结果时必须标明范围。
```
