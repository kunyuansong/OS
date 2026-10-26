# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
|---|---|
| **实验名称** | Lab 2：物理内存与页表 |
| **小组成员** | 2411241-宋坤远、2413121-林科融、2410949-何翔 |
| **完成日期** | 2026-10-26（报告整理日期；如与实际提交日期不同，请据实调整） |
| **实验项目** | RISC-V ucore（`lab2_new`） |
| **报告状态** | 已记录三个主要代码模块及其测试；QEMU/GDB 页表翻译调试、实验截图、Git 提交信息仍需补充 |

### 小组分工

**练习与代码模块分工**

| 成员 | 负责的练习/模块 | 核心代码文件 |
|---|---|---|
| **2411241-宋坤远** | 物理内存探测和 Page 描述符初始化；First-Fit 原理分析 | `kern/mm/pmm.c`、`kern/mm/default_pmm.c`（阅读分析） |
| **2413121-林科融** | Best-Fit 连续物理内存分配算法；管理器接入及测试 | `kern/mm/best_fit_pmm.c`、`kern/mm/pmm.c` |
| **2410949-何翔** | Buddy System 分配器设计、实现和测试 | `kern/mm/buddy_pmm.c`、`kern/mm/buddy_pmm.h` |

**实验报告分工**

| 成员 | 负责撰写或核查的报告内容 |
|---|---|
| **宋坤远** | 第一部分实现及错误分析、First-Fit 思考题、物理内存与页表基础知识 |
| **林科融** | Best-Fit 的设计规格、四段式提示词、测试与算法改进分析 |
| **何翔** | Buddy System 实现与测试、两个四段式提示词、边界条件与测试覆盖说明 |
| **共同完成** | 实验环境、QEMU/GDB 调试记录（待补）、测试截图、实验总结、Git 仓库提交 |

> **资料与真实性说明**：本报告依照提供的《实验2》指导书、原始 `lab2.zip` 工程、实验报告模板，以及实际交流中提交的命令行输出整理。原始压缩包不是虚拟机中修改后的最终工程；最终代码细节应以提交仓库的 `git diff`/源码为准。下述四段式 Prompt 是将已完成的开发任务**规范化整理为可复现的正式提示词**；部分代码实际上是在 ChatGPT 指导下由人工复制、修改，并不能据此声称全部 Prompt 已原样发送给 Codex 或由 Codex 自动修改。报告不虚构未执行的调试、截图或提交记录。

---

## 一、实验目的

1. 理解 RISC-V Sv39 分页模式中物理地址、内核虚拟地址、物理页号（PPN）及 Page 描述符的区别与联系。
2. 掌握从设备树（DTB）取得物理内存范围，排除 OpenSBI、内核镜像和 Page 描述符数组占用空间，并初始化可用物理页的流程。
3. 理解 First-Fit 的空闲链表管理方法，实现 Best-Fit 连续物理页分配器，比较不同适配策略的优缺点。
4. 实现 Buddy System 的按 2 的幂次分块、拆分与伙伴合并，并设计覆盖正常行为和边界情况的测试。
5. 学习利用四段式提示词（`[PROMPT]`、`[RELY]`、`[GUARANTEE]`、`[SPECIFICATION]`）规范 AI 辅助内核开发，同时以实际编译、QEMU 和评分结果验证实现。
6. 在已建立启动页表的基础上理解 `satp`、页表映射与地址转换；指导书的 QEMU/GDB 深入调试任务单独记录，未做不得写为完成。

---

## 二、实验环境

### 2.1 基础环境

| 项目 | 实际信息或说明 |
|---|---|
| 操作系统 | Ubuntu Linux 虚拟机；主机及 Ubuntu 精确版本尚未采集 |
| 工程位置 | `~/riscv-lab/lab2_new` |
| 体系结构 | RISC-V RV64，Sv39 分页 |
| 构建工具 | `make`、`riscv64-unknown-elf-gcc`、`riscv64-unknown-elf-ld`、`riscv64-unknown-elf-objcopy` |
| 模拟器 | `qemu-system-riscv64`；具体 QEMU 版本待用 `qemu-system-riscv64 --version` 补记 |
| 固件 | 运行输出显示 OpenSBI v0.9 |
| 模拟 DRAM | `0x80000000`—`0x88000000`（128 MiB，右端不包含） |
| 物理页面大小 | 4 KiB（`PGSIZE = 4096`） |
| 编译/运行/评分命令 | `make`、`make qemu`、`make grade` |

### 2.2 AI 工具使用情况

| 成员 | AI 编程工具 | 底层模型 | 备注 |
|---|---|---|---|
| 2411241-宋坤远 | ChatGPT、Codex | GPT 系列模型 | 辅助代码编写、调试、错误分析及实验报告整理 |
| 2413121-林科融 | ChatGPT、Codex | GPT 系列模型 | 辅助 Best-Fit 算法设计、代码实现与测试 |
| 2410949-何翔 | ChatGPT、Codex | GPT 系列模型 | 辅助 Buddy System 算法设计、代码实现与测试 |

> 请在提交前补全每个人**实际**使用的工具和模型版本。不要把当前聊天的模型名称自动填写为虚拟机 Codex 使用的模型。

### 2.3 QEMU 启动配置调整

在初次 `make qemu` 时，OpenSBI 输出曾显示 `Domain0 Next Address: 0x0000000000000000`，未进入内核。改为直接指定内核镜像：

```bash
qemu-system-riscv64 -machine virt -nographic -bios default -kernel bin/ucore.img
```

此后固件输出 `Domain0 Next Address: 0x0000000080200000`，内核顺利进入 `DTB Init`。因此将 Makefile 中原有的：

```makefile
-device loader,file=$(UCOREIMG),addr=0x80200000
```

改为：

```makefile
-kernel $(UCOREIMG)
```

并相应调整 `tools/grade.sh` 的内核启动方式（只调整启动参数，不修改测试断言或评分规则）。这种修改属于环境兼容性修复，应随提交代码保留在 Git diff 中，并在报告中说明。

---

## 三、实验整体逻辑分析

### 3.1 本章节的逻辑主线

本实验的主线是 **“获知物理内存范围 → 为物理页建立描述符 → 把真正空闲的完整页交给分配器 → 按算法分配与回收物理页”**，关键调用路径如下：

```text
OpenSBI / QEMU 启动
       │
       ▼
entry.S：建立启动页表、开启 Sv39
       │
       ▼
kern_init() → dtb_init()：解析物理内存信息
       │
       ▼
pmm_init()
  ├─ init_pmm_manager()：选择并初始化管理器
  ├─ page_init()：生成 Page 描述符并注册空闲页
  └─ check_alloc_page()：调用当前管理器测试
       │
       ▼
alloc_pages(n) / free_pages(base,n) / nr_free_pages()
```

`entry.S` 中的启动映射已由实验框架提供；本实验完成的是**分页开启之后的初步物理内存管理**，并不是重新从零实现整套 Sv39 多级页表。

统一的 `struct pmm_manager` 抽象把公共接口与具体算法解耦；同一套 `alloc_pages`/`free_pages` 接口可以分别使用 First-Fit、Best-Fit 或 Buddy System。

### 3.2 功能的逐步实现

1. **建立物理页基础数据**：读取 DTB 内存信息并初始化 `page_init()`，避免把固件、内核或元数据错误地交给分配器。
2. **理解基线算法**：阅读 `default_pmm.c`，掌握空闲块链表的表示方式和 First-Fit 行为，作为新分配器的参考。
3. **实现 Best-Fit**：在已可管理的物理页基础上，替换候选空闲块的选择策略，实现更精确的连续分配，并通过课程自动评分。
4. **扩展 Buddy System**：按阶数组织空闲块，实现拆分、合并和内部碎片管理，设计独立测试来弥补自动评分不覆盖该模块的问题。
5. **恢复 Best-Fit 作为默认管理器**：在保留 Buddy 源码及测试的同时，按指导书要求让最终默认运行的管理器为 Best-Fit，再次运行回归评分。
6. **分析和记录 Sv39 地址翻译机制**：结合已有启动页表、`satp` 地址以及原理讲解；进一步的 QEMU 源码级 GDB 跟踪留待实际执行后补齐。

---

## 四、实验内容与实现

### 4.1 功能模块一：物理内存探测与 Page 描述符初始化

**负责人：2411241-宋坤远**  
**修改文件：`kern/mm/pmm.c`**

#### 4.1.1 模块功能描述

**必须实现/修改的函数：**

```c
static void page_init(void);
```

`page_init()` 从 DTB 读入 RAM 起点与大小，验证内存范围，确定可管理上界；根据内核镜像结尾计算 Page 描述符数组位置，初始化描述符状态，并将余下完整、未被占用的页面登记进当前 `pmm_manager`。

核心关系为：

```c
npage = PPN(managed_end);
size_t page_count = npage - nbase;
```

`npage` 是**物理页号上界**，不能当作描述符数组元素个数。`pages[i]` 对应的物理页号为 `nbase + i`，`pa2page()` 的真实实现也使用 `pages[PPN(pa) - nbase]`。在本次 128 MiB 的配置下，物理内存从 `0x80000000` 开始，物理内存总共有 32768 个 4 KiB 页面，但全部页面并非都可用。

#### 4.1.2 关键实现逻辑

1. 使用 `get_memory_base()` 和 `get_memory_size()` 取得 DTB 解析结果，并验证大小、边界和地址加法不溢出。
2. 设置 `va_pa_offset = PHYSICAL_MEMORY_OFFSET`，将虚拟地址 `end` 转换为物理地址。
3. 以 `ROUNDDOWN(min(mem_end, PHYSICAL_MEMORY_END), PGSIZE)` 为可管理范围的末端；将描述符数组放在内核末尾之后的页对齐位置。
4. 按 `npage - nbase` 个 `struct Page` 分配元数据空间，并检查其是否与可管理范围重叠。
5. 将所有描述符初始化为保留状态；计算数组结束后的 `free_start`，只将 `[free_start, managed_end)` 的完整页面通过 `init_memmap(pa2page(free_start), free_count)` 注册。
6. 如果 DTB 范围无效、数组容不下、剩余不足一页，调用 `panic()`，杜绝无效页注册。

运行实例中记录的关键数据：

```text
Physical memory: [0x0000000080000000, 0x0000000088000000), size 0x8000000 bytes
PMM free memory: [0x0000000080347000, 0x0000000088000000), 31929 pages
```

`0x80347000` 是该次编译中描述符数组和页对齐之后的空闲区起点；该值取决于内核代码及元数据布局，不应该硬编码。

#### 4.1.3 规范化四段式提示词（可复现实验）

> 以下是依据实际完成任务整理的规范化版本。原始交互中经历了 AI 建议、人工粘贴和人工纠错；本段不能代替真实 Codex 对话日志。

```text
[PROMPT]
任务：在 kern/mm/pmm.c 中实现 page_init()，完成 DTB 物理内存识别、Page 描述符初始化与空闲页注册。
操作要求：仅修改 lab2 function1 标记对应的实现；必须修改工程真实文件；保留其他逻辑、已有注释和测试；不要修改分配策略与公开接口。
输出要求：基于当前工程源码写入代码，执行编译及 QEMU 验证，报告 git diff 与所有真实错误。

[RELY]
可用信息来自真实工程：
- get_memory_base()、get_memory_size()、PADDR()、PPN()、ROUNDUP()、ROUNDDOWN()；
- struct Page、pages、npage、nbase、va_pa_offset、SetPageReserved()；
- 链接符号 end、PGSIZE、DRAM_BASE、PHYSICAL_MEMORY_END、PHYSICAL_MEMORY_OFFSET；
- init_memmap(struct Page *, size_t)、pa2page(uintptr_t)、panic()、cprintf()。
必须先读 pmm.h、memlayout.h、mmu.h、dtb.h，核对字段和接口，不能编造宏名。

[GUARANTEE]
必须实现：static void page_init(void);
不得重写 get_memory_base()、get_memory_size()、pa2page() 或公共的 alloc_pages()/free_pages()。

[SPECIFICATION]
page_init()
Pre-Condition：dtb_init() 已完成；init_pmm_manager() 已完成；链接符号 end 有效；启动 Sv39 映射可用。
Post-Condition：va_pa_offset/npage/pages 设置正确，描述符覆盖 [nbase,npage)；所有未登记空闲区的页保持保留状态；仅完整可用物理页被注册。
Case 1（有效 DTB）：验证起止地址及溢出，裁剪到内核能管理的上界，输出内存范围。
Case 2（内核与描述符占用）：描述符数组放在内核镜像后并对齐；检查数组空间充足；不会与可分配页重叠。
Case 3（存在完整空闲页）：从描述符数组末端向上页对齐，只将完整区间传给 init_memmap()。
Case 4（无效 DTB）：明确报错并停止，不得注册错误页面。
Case 5（不足一个空闲页）：不得以 n=0 调用 init_memmap()。
Requirements：严格区分 VA、PA、PPN、Page*；不硬编码空闲页数量；保持 First-Fit 的检查正常。
```

#### 4.1.4 实现迭代与问题处理

**第一次迭代：代码尚未完整可编译。**

- 现象：首次执行 `make` 时，在 `best_fit_pmm.c` 报出 `free_list undeclared`、`nr_free undeclared`。原因是 Makefile 同时编译该文件，即使当时默认选中的是 First-Fit，该文件中的现成测试仍然依赖管理器状态符号。
- 处理：在 Best-Fit 文件的标记区域先补上私有的 `free_area_t free_area` 及其两个访问宏，不删除和改写课程测试。

**第二次迭代：重复函数定义。**

- 现象：`pmm.c` 同时存在两个 `static void page_init(void)`，编译器报告 redefinition。
- 原因：先后粘贴了两个版本，而旧版本未移除。
- 处理：核对两个实现后删除前一个，保留包含内核地址范围与描述符容量检查的后一个版本。

**第三次迭代：成功编译，但 QEMU 启动链有问题。**

- 现象：使用旧 `-device loader` 的 `make qemu` 只打印 OpenSBI，未出现 `DTB Init`；`Domain0 Next Address` 为零。
- 处理：使用 `-kernel bin/ucore.img` 启动成功；调整 Makefile 和评分脚本的加载方式。该问题属于 QEMU/OpenSBI 启动参数，不是 `page_init()` 的分配逻辑错误。

**最终结果**：`make` 成功、QEMU 正常启动、`check_alloc_page() succeeded!`；恢复默认 Best-Fit 后，`make grade` 中的 Page Init 项为 `OK`。

---

### 4.2 练习：理解 First-Fit 连续物理内存分配算法

**负责人：2411241-宋坤远**  
**阅读文件：`kern/mm/default_pmm.c`**

#### 4.2.1 核心函数和过程

| 函数 | 功能 |
|---|---|
| `default_init()` | 初始化双向空闲链表和空闲页数计数器 `nr_free` |
| `default_init_memmap(base,n)` | 将 `[base,base+n)` 中的物理页变为空闲状态，以首页的 `property=n` 表示连续空闲块，按地址插入链表 |
| `default_alloc_pages(n)` | 遍历链表，选择**第一个** `property>=n` 的空闲块；必要时拆分剩余块 |
| `default_free_pages(base,n)` | 重置归还页的状态，将其插入地址有序链表，和左右相邻空闲块合并 |
| `default_nr_free_pages()` | 返回剩余空闲物理页总数 |

在 `struct Page` 中，`page_link` 将空闲块首页接入链表，`PG_property` 标记该 Page 为一个空闲块的首页，`property` 记录此空闲块连续拥有多少页。并不是每一页都单独作为空闲链表节点。

**示例**：地址升序的空闲块大小为 `[7, 3, 5]`，请求 2 页，First-Fit 会直接使用第一块（7 页）；释放并按地址合并相邻块后，系统降低外部碎片，但仍不能保证所有空闲页在物理地址上连续。

#### 4.2.2 First-Fit 是否有改进空间？

有。First-Fit 只按地址顺序查找第一个足够大的区间，速度较简单，但可能过早拆分大块，形成许多小碎片。改进思路包括：

- **Best-Fit**：遍历全部候选，选择可容纳需求的最小空闲块，尽量保留更大的连续块；代价是查找可能更慢，也可能留下难以再用的极小碎片。
- **Buddy System**：用按 2 的幂次分组的空闲链表加快块查找和合并；代价是内部碎片（例如申请 3 页实际分配 4 页）。
- **数据结构优化**：采用按大小维护的树或分级空闲链表，降低寻找合适块的成本；需要兼顾地址相邻块的合并。

因此 Best-Fit 不保证在所有负载下都优于 First-Fit，具体效果依赖请求大小分布、释放顺序和可用内存碎片状态。

---

### 4.3 功能模块二：Best-Fit 连续物理内存分配算法

**负责人：2413121-林科融**  
**修改文件：`kern/mm/best_fit_pmm.c`、`kern/mm/pmm.c`**

#### 4.3.1 模块功能与接口

```c
static void best_fit_init(void);
static void best_fit_init_memmap(struct Page *base, size_t n);
static struct Page *best_fit_alloc_pages(size_t n);
static void best_fit_free_pages(struct Page *base, size_t n);
static size_t best_fit_nr_free_pages(void);
const struct pmm_manager best_fit_pmm_manager;
```

为管理空闲区定义独立的模块内状态：

```c
static free_area_t free_area;
#define free_list (free_area.free_list)
#define nr_free   (free_area.nr_free)
```

这些符号只在 Best-Fit 翻译单元内有效，不与 `default_pmm.c` 中的管理器状态冲突。

#### 4.3.2 实现说明

- **初始化**：`best_fit_init()` 初始化 `free_list` 并清零空闲页数。
- **注册空闲区**：`best_fit_init_memmap()` 清理 Page 状态，并将整段内存作为一个空闲块加入地址有序链表。
- **选择最小合适块**：`best_fit_alloc_pages(n)` 逐个比较空闲块，保留所有 `property>=n` 的候选中 `property` 最小者；只有遍历完成之后才确定最终分配块。
- **拆分**：当选中的块大于所需页数，分配前部 n 页，余下的后部页重新构成空闲块。
- **回收合并**：`best_fit_free_pages()` 将释放块按地址插入并分别检查其前驱、后继能否合并。
- **统一注册**：`best_fit_pmm_manager` 将各内部函数赋给 `struct pmm_manager` 的函数指针。`pmm.c` 的 `init_pmm_manager()` 选择 `&best_fit_pmm_manager`。

Best-Fit 与 First-Fit 的真正区别发生在**分配时的候选块选择**：前者需要在本实现中检查全部可用块，后者找到第一个可用块即可停止。

#### 4.3.3 规范化四段式提示词（可复现实验）

```text
[PROMPT]
任务：实现 kern/mm/best_fit_pmm.c 中的完整 Best-Fit 物理页分配器，并在 kern/mm/pmm.c 中接入。
操作要求：直接修改实际工程；保持已有 basic_check() 和 best_fit_check() 完全不变；参考 default_pmm.c，不改变 pmm_manager 接口。
输出要求：完成编译和 QEMU 验证，报告修改的函数及关键设计，保留真实错误和 diff。

[RELY]
使用 pmm.h 中 struct pmm_manager 的 init/init_memmap/alloc_pages/free_pages/nr_free_pages/check 接口；
使用 memlayout.h 中的 struct Page、property、page_link、PG_property 标志；
使用 libs/list.h 中的 list_init/list_next/list_prev/list_add_before/list_del/le2page；
参考 default_pmm.c 的页面状态、空闲块回收与合并方法；
保留 best_fit_pmm.c 中已有两个测试函数；确认头文件声明 extern const struct pmm_manager best_fit_pmm_manager。

[GUARANTEE]
必须交付：
best_fit_init(void)
best_fit_init_memmap(struct Page *, size_t)
best_fit_alloc_pages(size_t)
best_fit_free_pages(struct Page *, size_t)
best_fit_nr_free_pages(void)
best_fit_pmm_manager 注册对象
在 init_pmm_manager() 中选择 best_fit_pmm_manager。
可增加必要的 static 辅助函数与模块内部 free_area。

[SPECIFICATION]
best_fit_init：Pre：尚未使用空闲区；Post：空链表、nr_free=0。
best_fit_init_memmap：Pre：base 指向 n>0 个已保留 Page；Post：n 页加入地址有序空闲表，计数增加 n。
best_fit_alloc_pages：Pre：n>0；Post：成功时返回连续 n 页的起始 Page*，从空闲表移除所分配部分，nr_free 减 n；失败返回 NULL 且状态不变。
Case 1：总空闲页数不足 n，立即失败。
Case 2：多个块均可满足，选 property 最小的块，而不是第一个块。
Case 3：块大小恰好为 n，移除整块。
Case 4：块大小大于 n，拆分并维护剩余空闲块的 property/flag/list 状态。
best_fit_free_pages：Pre：base、n 表示合法已分配连续页；Post：页面归还、nr_free 增 n，并与物理地址相邻的空闲块正确合并。
Requirements：保持链表地址有序；不得修改框架测试；全部页面状态宏与实际源码一致。
```

#### 4.3.4 实现迭代与问题处理

1. **初次编译时符号缺失**：`best_fit_pmm.c` 的测试使用 `free_list` 和 `nr_free`，但初始框架未定义，报错 `undeclared`；补齐 `free_area` 与访问宏后通过。
2. **实现与接入分开验证**：先加入 Best-Fit 函数和管理器结构，运行 `make`，确保生成镜像；随后才在 `pmm.c` 中将 `default_pmm_manager` 切换到 `best_fit_pmm_manager`。
3. **运行验证**：QEMU 启动后输出 `memory management: best_fit_pmm_manager` 和 `check_alloc_page() succeeded!`，说明本次分配器内置测试已通过。
4. **自动评分验证**：`make grade` 运行后显示 Page Init、Best-Fit 均为 `OK`，总分 `20/20`。

#### 4.3.5 Best-Fit 是否有进一步改进空间？

有。本实现使用地址有序双向链表保证回收时相邻合并方便，但分配时可能需要遍历所有空闲块，查找成本为 **O(k)**，其中 k 是空闲块数量。可用按空闲块大小排序的平衡树或分层空闲链表改善查找，也可以组合“按大小索引 + 按地址合并”的两种数据结构。需要注意 Best-Fit 虽然使每次分配的剩余块大小最小，但不能在所有场景下消除外部碎片；进一步优化应依据工作负载统计，而不只是算法名字。

---

### 4.4 功能模块三：Buddy System 伙伴分配器

**负责人：2410949-何翔**  
**新增文件：`kern/mm/buddy_pmm.c`、`kern/mm/buddy_pmm.h`**

#### 4.4.1 模块功能与主要接口

```c
static void buddy_init(void);
static void buddy_init_memmap(struct Page *base, size_t n);
static struct Page *buddy_alloc_pages(size_t n);
static void buddy_free_pages(struct Page *base, size_t n);
static size_t buddy_nr_free_pages(void);
static void buddy_check(void);
const struct pmm_manager buddy_pmm_manager;
```

实验采用 4 KiB 物理页；`BUDDY_MAX_ORDER = 15`，每个阶数 k 对应一个大小为 `2^k` 页的空闲块链表。该阶数上限与本工程 128 MiB RAM、32768 页的规模一致，是当前实验环境下的约束，而不是通用内核任意内存大小都适用的设定。

#### 4.4.2 分配、拆分、释放和合并

**初始空闲区初始化**：`buddy_init_memmap()` 将可能并非 2 的幂次、也可能不是最大块对齐的连续空闲区，拆成若干个符合位置对齐的 2 的幂次块，分别插入不同阶数链表。

**分配**：申请 n 页时计算最小 `2^k >= n`，从 k 阶向上查找第一个非空链表；如果只找到更大的块，则反复均分为两个等长伙伴，将不需要的一半放回较小阶数链表。当前实现把实际分配页数记录在返回块首页的 `property` 中；例如申请 3 页，实际占用 4 页。

**释放**：按记录的实际块大小回收，并使用：

```c
size_t partner_index = index ^ chunk;
```

计算伙伴首页的 Page 描述符数组下标。如果伙伴空闲且大小、阶数匹配，则合并为高一阶块；持续查找直到不能合并为止。

**注意**：这与按最小请求页数计费的 First-Fit/Best-Fit 不同。在 Buddy System 中，3 页请求实际消耗 4 页，因此空闲页计数应减少 4，释放时也应归还完整的 4 页。由此产生内部碎片。

#### 4.4.3 Buddy System 实现提示词（第一份，规范化四段式）

```text
[PROMPT]
任务：在没有 Buddy 框架的前提下，新建 kern/mm/buddy_pmm.c、buddy_pmm.h，生成可接入 ucore 的伙伴物理内存分配器。
操作要求：直接修改项目中的真实文件；不得删除 First-Fit/Best-Fit 代码和现有测试；实现后能通过统一 pmm_manager 接口被选择。
输出要求：说明各阶空闲链表、块拆分、伙伴合并、实际分配页数的计数规则；编译并记录结果。

[RELY]
使用现有 struct pmm_manager、struct Page、PG_property、page_link、pages、npage、nbase；
使用 libs/list.h 的双向链表操作和 assert；
项目物理页大小为 PGSIZE；当前 RAM 最大 128 MiB，因此在本实验的配置下可选择最大阶 15；
Page 描述符数组下标以 pages 为基准，不能直接将物理页号当下标。

[GUARANTEE]
新建并导出 buddy_pmm_manager；
实现 buddy_init、buddy_init_memmap、buddy_alloc_pages、buddy_free_pages、buddy_nr_free_pages、buddy_check；
允许新增 static 辅助函数管理阶数、链表节点和伙伴合并。

[SPECIFICATION]
buddy_init：Pre：管理器尚未初始化；Post：全部阶数链表为空，空闲页计数为零。
buddy_init_memmap：Pre：给定 n>0 个保留页面；Post：全部且仅这些页按满足 2^k 和边界对齐的块登记为可分配。
buddy_alloc_pages：Pre：n>0；Post：找到并返回实际 2^k 页块，实际消耗数量从空闲计数中扣除；不足返回 NULL。
Case 1：直接存在大小合适的伙伴块，取用。
Case 2：仅存在更大块，递归拆分并回收右半块。
Case 3：n 非 2 的幂次，向上取整并记录实际分配大小。
Case 4：没有可满足的连续伙伴块，返回 NULL。
buddy_free_pages：Pre：base 指向已分配块首页；Post：完整实际块归还，正确递归合并空闲伙伴，空闲计数恢复相应页数。
Requirements：每块大小为 2^k 页；块首页按阶数对齐；伙伴通过以描述符基址为参考的异或索引计算；不得越过可管理地址范围；保留已有统一分配器接口。
```

#### 4.4.4 Buddy System 测试提示词（第二份，规范化四段式）

```text
[PROMPT]
任务：在现有 buddy_pmm.c 上增补独立测试，覆盖 Buddy System 分配、释放、拆分、合并及边界情形。
操作要求：在保持分配器核心实现不变的前提下，完善 buddy_check() 并增加必要的 static 测试辅助函数；通过真实 QEMU 运行检查，不制造测试成功输出。
输出要求：说明测试覆盖哪些行为、断言验证什么、失败时如何定位。

[RELY]
使用 buddy_alloc_pages、buddy_free_pages、buddy_nr_free_pages；
使用 BUDDY_MAX_ORDER、buddy_free_list、struct Page 的 property/PG_property、pages/npage/nbase；
使用 assert、list_next、le2page，测试通过时可用 cprintf 输出消息。

[GUARANTEE]
必须完善 static void buddy_check(void)；
可新增 static void buddy_check_invariants(void)；
管理器的 .check 应指向 buddy_check；不得修改课程已有 Best-Fit 测试。

[SPECIFICATION]
buddy_check：Pre：伙伴管理器已经初始化并接管可用物理页。
Post：测试过程释放全部成功分配的块，空闲页总数与初值一致，无残留或重复登记块。
Case 1：分别申请 1/2/3/5 页，验证实际分配 1/2/4/8 页、块起点对齐及计数减少 15。
Case 2：申请超过当前总空闲页的数量，必须返回 NULL，且计数不变。
Case 3：以不同于申请顺序的顺序释放，验证所有页能够正确回收。
Case 4：申请 16 个单页，交错释放偶数位置和奇数位置，验证碎片化和重新合并后计数恢复。
Case 5：遍历各阶空闲链表，验证块起点对齐、块大小与阶数相符、总页数一致，并检查不应仍保持分离的同阶空闲伙伴。
Requirements：所有断言基于实际内存管理状态；不假设所有申请都来自连续同阶大块；QEMU 运行通过才能声称测试通过。
```

#### 4.4.5 测试覆盖分析

| 测试 | 覆盖的关键行为 | 能够发现的典型缺陷 |
|---|---|---|
| 申请 1、2、3、5 页 | 向上取整、阶数选择、块对齐 | 按请求页数而非实际块大小扣减、未正确拆分 |
| 分配前后空闲计数 | 资源总量守恒 | 计数重复减少、遗漏回收 |
| 请求超过总空闲页数 | 基本资源不足处理 | 超额分配、非法指针返回 |
| 交错释放 16 个单页 | 碎片化和回收合并 | 错误判断伙伴、链表操作不完整 |
| 遍历各阶链表验证结构不变量 | 块大小、位置对齐、可合并伙伴 | 同阶链表登记错误、未及时合并 |
| 结束后空闲数恢复 | 操作可逆性与资源泄漏 | 测试过程残留分配 |

需要说明，本测试**没有**全面覆盖所有非法指针输入、重复释放、并发竞争或极端 OOM 状态，也不能据此断言 Buddy System 经过生产级验证。

#### 4.4.6 实现迭代及结果

1. 新建 `buddy_pmm.h` 导出 `buddy_pmm_manager`。
2. 新建 `buddy_pmm.c`，实现阶数链表、初始拆分、分配和伙伴合并；`make` 成功。
3. 在 `pmm.c` 中临时选择 `buddy_pmm_manager`，`make qemu` 首次成功，输出 `check_alloc_page() succeeded!`，但该时点的 `buddy_check()` 仅测试单页分配/释放。
4. 将测试扩展为多页数、内存不足、交错释放与链表结构不变量检查；再次运行 QEMU 输出：

```text
memory management: buddy_pmm_manager
PMM free memory: [0x0000000080347000, 0x0000000088000000), 31929 pages
buddy_check: detailed tests passed!
check_alloc_page() succeeded!
```

5. 详细测试通过后，为满足指导书要求，把默认管理器恢复成 `best_fit_pmm_manager`，保留 Buddy 文件和测试代码，并重新执行 `make grade`，获得 `20/20`。

---

### 4.5 Challenge 思考题：若 OS 事先不知道硬件物理内存范围，如何探测？

**状态：思考题回答；不是新增实现功能。**

操作系统不能凭任意地址读写来可靠探测 RAM，因为物理地址空间中可能存在 MMIO、地址空洞和固件保留区。更安全的办法是利用由固件或引导程序维护的机器内存布局描述：

- 本实验 RISC-V/QEMU 场景中，由引导固件将设备树（DTB/FDT）地址通过寄存器交给内核；内核在 `dtb_init()` 中解析 memory 节点，取得 RAM 起点和长度。
- 其他平台可能使用 BIOS 的 E820 内存映射、UEFI Memory Map、ACPI 表或固件提供的其他硬件描述协议。
- 取得内存布局后，内核还必须进一步排除固件、内核、页表、内存管理元数据等保留区，并进行页边界对齐，才能把页面加入物理内存分配器。

因此“获取 DRAM 范围”和“得到可以安全分配的页面”是两个不同阶段。

### 4.6 Challenge：SLUB 任意大小对象分配器

**状态：未实现、未测试，不作为已完成加分项申报。**

可行的设计思路是两层架构：底层 Buddy/页分配器管理整页，上层按对象大小维护 `kmem_cache` 或不同 size class 的 slab，单个 slab 页内通过 freelist 管理可用对象。需要独立测试小对象、跨页对象、释放复用、边界对齐等情况。由于没有实际提交此功能的代码或运行日志，报告仅说明设计方向，不给出虚假的完成结果。

### 4.7 分支任务：QEMU/GDB 观察 Sv39 虚拟地址翻译

**状态：尚未完成实际源码级单步调试。本节为知识准备和待补的实验记录框架。**

#### 4.7.1 已有理论基础

RISC-V Sv39 的 39 位有效虚拟地址划分为三级各 9 位索引和 12 位页内偏移。`satp` 保存分页模式与根页表物理页号。TLB 命中时可以利用缓存的地址翻译直接完成访问；未命中时需执行页表遍历。有效页表项的 PPN 字段给出物理页号，结合页内偏移形成物理地址。权限位 R/W/X、U、A、D 等用于控制合法访问与记录状态。

**注意：** `entry.S` 中使用了 1 GiB 叶子页表项建立启动内核映射，因此实际访问可能在根级页表项处就完成转换，不能机械地宣称每次访问都逐级读取三个页表页。

已观测到的内核地址示例：

```text
satp virtual address: 0xffffffffc0205000
satp physical address: 0x0000000080205000
```

此处输出的是**启动页表的虚拟/物理地址**，不是对 QEMU 内部 TLB 查找代码的调试证据。

#### 4.7.2 指导书要求的调试工作（待实际开展）

1. 准备带调试符号的 QEMU 4.1.1（或记录实际采用版本），不要替换系统原有 QEMU。
2. 启动调试版 QEMU，通过主机 GDB 附加模拟器进程；另用 RISC-V GDB 连接 QEMU 的调试接口观察内核指令。
3. 选择明确的访存或取指虚拟地址，记录该地址、寄存器状态、实际断点和调用路径。
4. 在 QEMU 源码中定位 TLB 查找、TLB miss 后的页表遍历和权限检查关键分支，逐条记录变量变化与最终物理地址。
5. 比较开启/未开启地址翻译时的模拟路径，区分模拟器软件 TLB 与真实处理器硬件 TLB 的不同。
6. 保留实际 GDB 截图及命令输出后补写“调试调用路径”“关键分支”“单步观察”和“遇到的问题”。

**本节待补实证信息**：QEMU 版本、QEMU 源码函数路径、断点设置、具体虚拟地址与物理地址、单步截图、TLB 命中/未命中证据。当前不能提供经实验验证的 QEMU 内部调用路径。

---

## 五、测试与验证

### 5.1 编译及内核运行

**物理内存初始化 + First-Fit 阶段**：`make` 与 `make qemu` 成功。QEMU 输出曾包含：

```text
memory management: default_pmm_manager
Physical memory: [0x0000000080000000, 0x0000000088000000), size 0x8000000 bytes
PMM free memory: [0x0000000080347000, 0x0000000088000000), 31929 pages
check_alloc_page() succeeded!
```

**切换 Best-Fit 阶段**：`make qemu` 成功，输出：

```text
memory management: best_fit_pmm_manager
check_alloc_page() succeeded!
```

**Buddy System 详细测试阶段**：

```text
memory management: buddy_pmm_manager
buddy_check: detailed tests passed!
check_alloc_page() succeeded!
```

上述结果分别反映**不同时间点**的运行状态。Buddy 测试时内核临时使用 Buddy，最终默认管理器恢复为 Best-Fit，二者并非同时生效。

### 5.2 课程评分（最终回归测试）

将默认管理器恢复为 Best-Fit 后，再次执行 `make grade`，得到满分。已记录的一次完整输出为：

```text
lab2 injection complete
...
  -check page init:                          OK
  -check best fit:                           OK
Total Score: 20/20
```

**结论**：课程提供的 Page Init 和 Best-Fit 自动评分项均通过，获得 `20/20`。评分脚本不覆盖 Buddy System，因此 Buddy 的证据是独立 QEMU 详细测试，而不是该 20 分的一部分。

### 5.3 验收表

| 验收项 | 证据 | 结果 |
|---|---|---|
| 第一部分 Page 描述符初始化 | `make qemu` + `make grade` | 通过 |
| 第二部分 Best-Fit | `make qemu` + `make grade` | 通过 |
| 第三部分 Buddy System | `make qemu` + `buddy_check: detailed tests passed!` | 通过当前详细测试 |
| 恢复默认 Best-Fit 后的回归测试 | 再次 `make grade` | `20/20` |
| QEMU/GDB 双层源码调试 | 尚无实际单步日志 | **待完成** |
| SLUB Challenge | 尚无模块或测试 | **未完成（选做）** |
| Git 远程仓库提交 | 尚无 `git remote`/`git push` 证据 | **待核实** |

### 5.4 测试截图（提交前补入）

请在虚拟机中将真实截图保存于项目的 `images/` 文件夹，建议：

- `images/page_init_qemu.png`：Page Init / First-Fit 测试成功输出。
- `images/best_fit_qemu.png`：启用 Best-Fit 的 `check_alloc_page() succeeded!`。
- `images/buddy_detailed_test.png`：Buddy 详细测试成功输出。
- `images/make_grade_20_20.png`：最终 `Total Score: 20/20`。
- `images/qemu_gdb_translation.png`：**仅在实际完成 GDB 调试后**补入。

> 目前没有收到以上真实运行截图，故不在 Markdown 中插入指向不存在文件的图片链接。补齐图片后可在本处使用 `![说明](./images/文件名.png)`。

---

## 六、实验总结与收获

### 6.1 对操作系统的理解

#### 重要知识点及与 OS 原理的对应关系

| 实验中的具体机制 | 对应的 OS 原理 | 联系与区别 |
|---|---|---|
| DTB 物理内存描述 | 启动时内存发现与物理资源枚举 | DTB 给出 RAM 分布，但未直接给出可以无条件分配的页面 |
| `struct Page`、`pages` 数组 | 页帧元数据（page frame metadata） | 每个描述符记录状态；描述符内存自身也必须占用真实物理页 |
| `PADDR()`、`pa2page()`、`npage/nbase` | VA/PA/PPN 与地址映射 | 虚拟地址到物理地址的映射不等于物理页号到 Page 指针的索引转换 |
| First-Fit/Best-Fit | 连续内存分配与外部碎片 | 选块策略不同；可用总页数足够不代表存在足够连续的块 |
| Buddy System | 2 的幂次块分配与伙伴合并 | 合并定位方便；向上取整产生内部碎片 |
| `pmm_manager` 函数指针表 | 策略和机制分离 | 内核上层接口保持不变，底层算法可以切换 |
| Sv39 启动页表与 `satp` | 分页地址翻译 | 页表把虚拟页映射到物理页；当前项目主要使用启动时的高地址大页映射 |
| `make grade` 与内置 assert | 测试验证与不变量 | 断言能够检查指定性质，但测试通过不等于覆盖所有运行情况 |

#### 本实验未充分覆盖但同样重要的 OS 知识点

- **缺页异常及按需分页**：遇到未映射虚拟页时如何触发异常、按需分配并更新页表。
- **页面置换和 Swap**：物理内存不足时如何选择页面换出、从外存换入。
- **进程独立地址空间与隔离**：不同进程各自的根页表、页权限和地址空间切换。
- **写时复制（Copy-on-Write）**：多进程共享只读页，在写入时复制页面。
- **TLB 一致性与多核同步**：页表修改后，多个处理器的 TLB 如何失效与同步。
- **内存分配并发安全**：本次实现主要验证单核、单线程初始化分配，没有完整锁和竞态控制。
- **SLUB 类对象分配器**：本次未真正实现面向任意对象大小的二级分配层。

### 6.2 AI 协作开发的经验

1. **明确作用范围**：例如只允许补全 `lab2` 标记处，且严禁修改框架测试，能够减少 AI 为了编译通过而破坏测试的风险。
2. **上下文以真实代码为准**：只有核对 `pmm.h`、`memlayout.h` 等文件后，才知道 `pages[i]` 对应 `nbase + i`，而不是错误地直接以 PPN 作为数组下标。
3. **接口与行为规格分开**：`[GUARANTEE]` 描述必须实现哪些函数；`[SPECIFICATION]` 规定正常情况、异常边界与全局状态不变量；二者比“帮我写一个内存管理器”更便于核对。
4. **编译通过不等于功能通过**：最初 `make` 只说明链接成功，只有切换到 Best-Fit/Buddy 并实际运行相应检查才能证实该管理器被调用。
5. **保留失败过程**：此次实际遇到 Best-Fit 测试依赖的宏未定义、`page_init` 重复定义、旧 QEMU 加载参数无法进入内核三个问题。分清源码逻辑故障与环境故障，有助于缩小排查范围。
6. **不能把 AI 结果视为最终证据**：由于 Codex Token 用尽，后续主要通过对话生成代码并由人工放入虚拟机。实验报告要区分“Prompt 设计”“实际如何改文件”和“终端实际输出”，不能把规范化后补写的提示词伪装成真实 Codex 操作日志。
7. **模块分阶段验证**：先保存 20/20 备份，再新增 Buddy；Buddy 通过详细测试后恢复默认 Best-Fit 并做回归评分，降低新模块影响必做项的风险。

### 6.3 结论与待完成事项

本实验完成了物理内存初始化、Best-Fit 连续页面分配，以及 Buddy System 的拆分、分配、释放和伙伴合并。课程自动评分 **Page Init + Best-Fit = 20/20**；Buddy System 在独立 QEMU 详细测试中通过。

**提交前必须处理的事项**：

- [ ] 补充三位同学实际使用的 AI 工具和模型版本。
- [ ] 补入真实 QEMU 和 `make grade` 截图。
- [ ] 按课程要求，确认/替换源码中 `YOUR CODE` 的占位文本，且保留 `lab2` 标记。
- [ ] 完成指导书中的 QEMU/GDB 页表翻译调试，补充具体函数调用路径、断点、单步证据与分析；如果不开展，应在提交时明确说明缺项。
- [ ] 核对最终代码是否包括 `buddy_pmm.c/.h`，且 `pmm.c` 默认仍选择 Best-Fit。
- [ ] 完成 Git 提交与远程推送，并补入仓库链接及最终提交哈希。
- [ ] 根据实际完成日期更正报告首页日期。

---

## 附录 A：关键命令与复现实验步骤

```bash
# 工程目录
cd ~/riscv-lab/lab2_new

# 构建
make

# 启动内核（当前 Makefile 已改为 -kernel 方式）
make qemu

# 课程自动评分；期望 Page Init、Best-Fit 均通过
make grade

# 关键结果自查：应为 best_fit_pmm_manager
grep -n 'pmm_manager = ' kern/mm/pmm.c

# 检查三个模块文件与代码差异
ls kern/mm/buddy_pmm.c kern/mm/buddy_pmm.h
git status --short
git diff -- kern/mm/pmm.c kern/mm/best_fit_pmm.c kern/mm/buddy_pmm.c Makefile tools/grade.sh

# 补充环境版本信息
qemu-system-riscv64 --version | head -n 1
riscv64-unknown-elf-gcc --version | head -n 1
```

## 附录 B：提交信息（实际推送后填写）

|| 项目 | 内容 |
|---|---|
| Git 托管平台 | GitHub |
| 远程仓库地址 | https://github.com/kunyuansong/OS |
| 最终主分支默认管理器 | `best_fit_pmm_manager` |

