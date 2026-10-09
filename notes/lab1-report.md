# uCore Lab1 实验报告：最小内核与启动流程

## 1. 实验目的、环境与证据说明

本实验理解 RISC-V 最小内核从复位到执行 C 代码的过程，掌握交叉编译、链接布局、启动栈、SBI 输出和 QEMU/GDB 远程调试方法。

| 项目 | 本次环境 |
|---|---|
| 项目目录 | `~/riscv-lab/lab1/lab1` |
| 模拟平台 | `qemu-system-riscv64`，`-machine virt -nographic -bios default` |
| 编译与链接 | Makefile 配置的 `riscv64-unknown-elf-` 工具链，GNU make |
| 调试器 | 用户实际使用 `gdb-multiarch`，连接 QEMU TCP 1234 |
| 内核 | RISC-V 64 位 ELF，入口 `0x80200000` |
| 固件 | QEMU 默认 OpenSBI，本次入口 `0x80000000` |

工具版本、宿主系统发行版未提供，不引用指导文档示例版本作为本次环境版本。

本文区分三类证据：**实测**指用户提供的已完成编译、ELF 检查及 QEMU/GDB 记录；**源码推导**指读取当前项目得到的实现解释；**未验证**指没有相应运行记录的细节。整理报告期间只读取文件，没有重新编译或运行 QEMU/GDB。

依据为 `lab1.docx`、`notes/lab1-analysis.md` 及下文标注的真实源码。原分析笔记是运行前的静态分析，其“待验证”状态和旧 loader 启动链不能代替本次实测。

## 2. 正式练习与章节整体逻辑

`lab1.docx` 中全部正式练习为：

1. **练习 1：理解内核启动中的程序入口操作。** 阅读 `kern/init/entry.S`，说明 `la sp, bootstacktop` 和 `tail kern_init` 完成的操作及目的。
2. **练习 2：使用 GDB 验证启动流程。** 跟踪 QEMU 模拟的 RISC-V 从加电到内核第一条指令，回答最初几条指令的地址与功能，并简要记录调试过程和观察结果。

文档中的现代笔记本启动流程属于拓展介绍，tmux 和内存观察属于工具介绍或实操建议，不另列为必做练习。

本章的逻辑是：先确定程序布局与入口，再交叉编译并链接内核；由 QEMU 与固件建立执行环境，汇编入口设置栈并转入 C；C 初始化清零 BSS，通过分层输出接口请求 SBI 服务，最后停留在无限循环；用 GDB 验证上述控制流。它实现的是最小内核启动基础，而非完整操作系统。

文档另要求 Markdown 文本报告、整体逻辑说明、核心模块理解、全部练习答案、知识点与 OS 原理的联系和差异，以及未涉及的重要知识点，本文均予覆盖。文档还要求小组通过 Git 网站提交；按本任务约束，仅生成 `notes/lab1-report.md`，仓库上传和课程提交留给人工完成。

## 3. 项目结构与关键模块

| 文件 | 核心职责 |
|---|---|
| `kern/init/entry.S` | 导出 `kern_entry`，设置启动栈，尾跳转；在 `.data` 预留栈 |
| `kern/init/init.c` | `kern_init` 清零 BSS、输出启动消息、无限循环 |
| `kern/mm/mmu.h`、`kern/mm/memlayout.h` | 定义页大小、页移位及栈大小，没有实现页表或内存分配 |
| `tools/kernel.ld` | 设定 ELF 入口、链接基址、节布局与边界符号 |
| `kern/libs/stdio.c` | `cprintf`、`vcprintf` 和字符输出回调 |
| `libs/printfmt.c` | `vprintfmt` 解析格式串并逐字符调用回调 |
| `kern/driver/console.c` | `cons_putc` 转交 SBI；初始化及中断函数为空 |
| `libs/sbi.c` | `sbi_call` 用 `ecall` 请求固件服务 |
| `libs/string.c` | 提供内核自己的 `memset` 等内存操作 |
| `Makefile`、`tools/function.mk` | 源文件收集、依赖生成、编译、链接、镜像转换及启动命令 |

Makefile 提到 `kern/debug`、`kern/trap` 等目录，但当前实际文件列表没有对应实现，不能据目录名认为异常处理或调试监控器已实现。

## 4. 内核构建与程序布局

### 4.1 构建流程

根据 `Makefile` 和 `tools/function.mk` 的 `listf`、`cc_template`、`read_packet` 等规则：

```text
kern 与 libs 中指定目录的 .c/.S
  → GCC 生成 obj/ 中的 .o，并生成 .d 依赖
  → ld -T tools/kernel.ld 生成 bin/kernel
  → objcopy --strip-all -O binary 生成 bin/ucore.img
```

大写 `.S` 允许预处理，入口可使用头文件宏。编译选项包括 `-mcmodel=medany -O2 -g -nostdinc -fno-builtin`，以及函数、数据分节选项；链接使用 `-m elf64lriscv -nostdlib --gc-sections`，不依赖宿主标准库，并回收未使用的节。规则还产生 `obj/kernel.asm` 和 `obj/kernel.sym`。

**实测：** 用户执行 `make -j1` 成功，生成 `bin/kernel`、`bin/ucore.img`；用 `readelf` 和 `nm` 确认 ELF 为 RISC-V 64，并取得：

| 符号 | 实测地址 |
|---|---|
| `kern_entry` | `0x80200000` |
| `kern_init` | `0x8020000a` |
| `bootstacktop` | `0x80203000` |

未提供完整命令参数及输出，因此不补造检查日志。

### 4.2 ELF 与原始镜像

| 属性 | `bin/kernel` | `bin/ucore.img` |
|---|---|---|
| 格式 | ELF 可执行文件 | 原始二进制 |
| 元数据 | 包含入口、装载信息及符号/调试信息 | 不含 ELF 头、入口及符号信息 |
| 用途 | 当前 QEMU `-kernel` 加载对象；GDB 符号来源 | 需由加载配置另行给出地址和启动安排 |

`ucore.img` 是 `objcopy -O binary` 的结果，不是带文件系统或分区表的磁盘镜像。BSS 通常不保存初始化字节，需要启动时清零；本次未提供完整 ELF 节/段表，不报告具体文件偏移或段大小。

`tools/kernel.ld` 使用 `OUTPUT_ARCH(riscv)`、`ENTRY(kern_entry)` 和 `BASE_ADDRESS = 0x80200000`。布局依次为 `.text`、`.rodata`、按 `0x1000` 对齐的 `.data`、`.sdata`、`edata`、`.bss`（含 `.sbss*`）、`end`。`ENTRY` 设置 ELF 元数据，实际 PC 转移仍需要启动链完成。`ALIGN(0x1000)` 表示 4096 字节对齐。

## 5. 启动链与练习 2 解答

### 5.1 初始地址和复位指令功能

**直接答案：** 本次 QEMU 虚拟 CPU 初始 PC 为 `0x1000`。这里执行的是 QEMU Reset ROM 的短引导代码；它取得 hart ID、准备启动参数并跳转到 `0x80000000` 的 OpenSBI 固件入口。经固件初始化后，控制权交给 `0x80200000` 的 uCore 入口。这些地址和参数是本次环境实测，不是所有 QEMU 版本或所有 RISC-V 硬件的固定结果。

GDB 实测反汇编及其语义如下：

| 地址 | 指令 | 作用（根据指令与实测数据解释） |
|---|---|---|
| `0x1000` | `auipc t0,0x0` | 将当前 PC `0x1000` 放入 `t0`，作为 ROM 内数据的寻址基准 |
| `0x1004` | `addi a2,t0,40` | 得到 `a2 = 0x1028`，指向 ROM 内启动信息 |
| `0x1008` | `csrr a0,mhartid` | 读取 hart ID，本次为 `0x0` |
| `0x100c` | `ld a1,32(t0)` | 从 `0x1020` 读取 `0x87000000` 到 `a1` |
| `0x1010` | `ld t0,24(t0)` | 从 `0x1018` 读取固件入口 `0x80000000` 到 `t0` |
| `0x1014` | `jr t0` | 跳到 `0x80000000`；语义为不写返回地址的间接跳转 |

ROM 数据实测为：

| 数据地址 | 读取值 |
|---|---|
| `0x1018` | `0x80000000` |
| `0x1020` | `0x87000000` |
| `0x1028` | `0x4942534f` |

`0x4942534f` 按小端字节可解释为 `OSBI` 标识。结合启动接口语义，`a1` 可解释为设备树地址，`a2` 为固件动态启动信息指针；本次没有解析该设备树或完整信息结构，因此其完整内容不属于已验证结果。上述参数是在 ROM → 固件交接处观测的，不能直接当作内核入口参数快照。

### 5.2 实际 GDB 跟踪过程

以下整理用户已完成的操作与结果，不补充未提供的完整命令历史：

1. 修复启动配置后执行 `make debug`，QEMU GDB Server 成功监听 TCP 1234。当前规则的 `-s` 提供调试端口，`-S` 暂停初始 CPU 执行。
2. 使用 `gdb-multiarch` 连接 QEMU，确认初始 `pc = 0x1000`，反汇编六条复位指令并读取上表数据。
3. 单步执行前五条指令，取得 `pc = 0x1014`、`t0 = 0x80000000`、`a0 = 0x0`、`a1 = 0x87000000`、`a2 = 0x1028`。
4. 执行 `jr t0` 后，`pc = 0x80000000`，实际进入 OpenSBI 固件入口。
5. 在 `0x80200000` 设置硬件断点并继续执行，成功命中 `kern_entry`，验证固件最终交接到内核。
6. 在入口单步验证栈初始化与尾跳转，具体结果见练习 1。
7. 继续运行，QEMU 输出 `(THU.CST) os is loading ...`；暂停后 `pc = 0x8020003a`，对应 `j 0x8020003a`，确认最终忙循环。

这条跟踪链验证了 ROM、固件入口、内核入口及内核输出。OpenSBI 内部阶段采用继续运行至断点的方法，**没有逐条跟踪其全部初始化指令**。

```text
0x1000：Reset ROM（准备启动参数）
  → 0x80000000：OpenSBI（固件初始化与交接）
  → 0x80200000：kern_entry（建立内核栈）
  → 0x8020000a：kern_init（清零、输出）
  → 0x8020003a：无限循环
```

指导文档 tips 将复位处概括为 OpenSBI，并建议通过 watch 观察“固件加载内核”。本次应更精确地区分 Reset ROM 与 OpenSBI：内核由 QEMU 启动加载配置预先装载，固件负责交接；没有证据证明 OpenSBI 执行了内核镜像复制。本文不声称执行过该 watch 验证。

## 6. 练习 1 解答：建立栈并进入 C 内核

`kern/init/entry.S` 的核心代码：

```asm
kern_entry:
    la sp, bootstacktop
    tail kern_init

.section .data
    .align PGSHIFT
bootstack:
    .space KSTACKSIZE
bootstacktop:
```

### 6.1 `la sp, bootstacktop`

**直接答案：** 将 `bootstacktop` 的地址放入栈指针 `sp`，为后续 C 函数的栈帧、局部变量及寄存器保存提供内核栈。它取得地址，不读取该地址处的内容。

`kern/mm/mmu.h` 定义 `PGSIZE = 4096`、`PGSHIFT = 12`；`memlayout.h` 定义 `KSTACKPAGE = 2`，故 `KSTACKSIZE = 8192`。`.align PGSHIFT` 在此 RISC-V 汇编中要求 4096 字节对齐，`.space` 在 `.data` 预留栈区。栈区为 `[bootstack, bootstacktop)`，初始栈指针位于高地址端，按调用约定向低地址增长，并满足 16 字节栈对齐。

`la` 是伪指令，实际展开受链接与松弛影响。本次反汇编为：

```asm
0x80200000: auipc sp,0x3
0x80200004: mv sp,sp
```

`auipc` 计算 `0x80200000 + (0x3 << 12) = 0x80203000`；后面的 `mv sp,sp` 是零偏移地址形成操作的反汇编显示，语义等价于 `addi sp,sp,0`，值不变。不能把这一具体展开推广为所有 `la` 的固定展开。

| 时刻 | `sp` 实测值 |
|---|---|
| 执行 `la` 前 | `0x80017ee0` |
| 执行 `auipc sp,0x3` 后 | `0x80203000` |
| 执行 `mv sp,sp` 后 | `0x80203000` |

前一个值是进入内核时继承的上下文，本次入口将其替换为自己的栈顶。由栈顶和栈大小可推导栈起点为 `0x80201000`，该起点不是用户提供的独立符号测量值。

### 6.2 `tail kern_init`

**直接答案：** 将控制权交给不返回的 `kern_init`，使用已建立的栈继续初始化；尾跳转不为当前入口建立新的 `ra` 返回地址。一般尾调用伪指令可采用 PC 相对地址形成与间接跳转，本次最终链接结果简化为直接跳转：

```asm
0x80200008: j 0x8020000a <kern_init>
```

`j` 表示不保存链接地址的跳转（语义为 `jal x0,目标`；具体编码可由压缩指令实现）。本次地址间隔表明该跳转占 2 字节。

| 寄存器 | 执行前 | 执行后 |
|---|---|---|
| `pc` | `0x80200008` | `0x8020000a` |
| `sp` | `0x80203000` | `0x80203000` |
| `ra` | `0x800078cc` | `0x800078cc` |

`ra` 保持原值是本次单步确认的事实，并非入口主动设置的返回点。`kern_init` 声明为 `noreturn` 且最终无限循环，因此无需返回汇编入口。后续 C 函数执行可能调整 `sp`，上表只说明尾跳转本身的效果。

## 7. `kern_init`：清零、输出和无限循环

`kern/init/init.c`：

```c
int kern_init(void) __attribute__((noreturn));
int kern_init(void) {
    extern char edata[], end[];
    memset(edata, 0, end - edata);
    const char *message = "(THU.CST) os is loading ...\n";
    cprintf("%s\n\n", message);
    while (1)
        ;
}
```

**源码推导：** `edata`、`end` 来自 `tools/kernel.ld`，是地址边界符号，声明不会另外分配数组。`memset` 清零 `[edata, end)`，覆盖 BSS 及可能的前置对齐填充，以满足未显式初始化静态存储对象的零初始化要求。`libs/string.c::memset` 的默认实现逐字节写入；启动栈在 `.data`，位于此清零范围之前，不会被该操作清除。本次没有提供清零前后的内存快照，也没有给出 BSS 实际长度。

输出消息经 `%s` 格式化。消息自身含一个 `\n`，格式串再追加两个，故源码预期末尾共有三个换行，不输出字符串终止符；实测确认可见启动文本，未单独统计换行字节。

`while (1)` 是持续执行的忙循环，没有 `wfi`、关机或进程退出操作。**实测：** 暂停后 `pc = 0x8020003a`，指令 `j 0x8020003a`，与源码一致。`noreturn` 告知编译器函数不返回，真正阻止返回的是控制流中的无限循环。

## 8. SBI 字符输出调用链

```text
kern_init
  → cprintf → vcprintf → vprintfmt → cputch
  → cons_putc → sbi_console_putchar → sbi_call
  → ecall → OpenSBI 控制台服务 → 返回调用链
```

`kern/libs/stdio.c::cprintf` 组织可变参数，`vcprintf` 将 `cputch` 和计数器交给 `libs/printfmt.c::vprintfmt`。格式解析器对普通字符直接回调，对 `%s` 逐字符读取字符串并回调。`cputch` 调用 `cons_putc` 并递增计数；该计数代表尝试输出的字符数，不是硬件成功发送的确认。

`kern/driver/console.c::cons_putc` 将字符转换为 `unsigned char`，交给 `libs/sbi.c::sbi_console_putchar`。后者调用 `sbi_call(SBI_CONSOLE_PUTCHAR, ch, 0, 0)`，其中服务号为 1。核心内联汇编意图为：

```asm
mv x17, %[sbi_type]    # a7：服务号 1
mv x10, %[arg0]        # a0：字符
mv x11, %[arg1]        # a1：0
mv x12, %[arg2]        # a2：0
ecall
mv %[ret_val], x10     # 从 a0 取得返回值
```

这是 legacy SBI 字符输出接口形态。S 模式内核通过环境调用请求固件服务，由固件完成底层控制台输出，内核此路径没有直接写 UART 寄存器。**实测**启动字符串成功输出，支持此输出路径在当前构建中可用；**源码推导**给出其分层机制，未逐条调试 `ecall` 陷入、固件串口操作或返回过程。

## 9. 原 Makefile 启动问题：定位、修复与验证

用户原先使用的配置为：

```make
-device loader,file=$(UCOREIMG),addr=0x80200000
```

**实测现象：** OpenSBI 显示 `Domain0 Next Address = 0x0`，未观察到内核启动消息。ELF 的入口和符号检查已确认内核入口为 `0x80200000`，因此需要检查装载配置与固件下一阶段地址的交接，而不能仅凭没有输出判断 C 内核有错误。

**定位解释：** 将镜像放到指定内存地址，与向启动链提供下一阶段入口信息是两个环节。本次 loader 配置没有产生正确的 OpenSBI Next Address。现有记录没有深入审计 QEMU/OpenSBI 内部实现，故不将此现象归结为所有版本的 loader 必然失败，也不宣称已证实具体内部参数生成错误。

用户已将 `qemu`、`debug` 两个目标改为加载 ELF：

```make
$(V)$(QEMU) \
    -machine virt \
    -nographic \
    -bios default \
    -kernel $(kernel)
# debug 目标另加 -s -S
```

当前 `git diff -- Makefile` 与上述修复一致。两目标仍依赖 `$(UCOREIMG)`，但启动实际传入的是 `$(kernel)`，即 `bin/kernel`；`SWAPIMG`、`SFSIMG` 在当前文件中没有定义，不能据此声称构建了交换区或文件系统镜像。

| 验证项 | 用户实测结果 |
|---|---|
| `make qemu` | 启动成功，打印内核消息 |
| OpenSBI 下一阶段地址 | `Domain0 Next Address = 0x80200000` |
| OpenSBI 下一阶段模式 | `Domain0 Next Mode = S-mode` |
| `make debug` | QEMU GDB Server 启动成功，TCP 1234 监听正常 |
| 内核入口断点 | 命中 `0x80200000` 的 `kern_entry` |

日志表明固件配置为交接到 S 模式，断点验证实际到达内核；未提供内核入口特权状态 CSR 的独立读取。Makefile 的 `gdb` 目标仍硬编码 `riscv64-unknown-elf-gdb`，而用户实际使用 `gdb-multiarch`，不将两者混写为同一条已执行命令。

本次报告生成没有再次修改 Makefile，也没有重新运行上述命令。

## 10. 与操作系统及体系结构知识的联系

| 本实验知识点 | 原理含义、联系与差异 |
|---|---|
| 复位与分阶段引导 | CPU 先执行平台复位代码，再由固件交接操作系统；本实验是 QEMU 虚拟平台，不等于物理机器通用复位地址 |
| M/S 特权级与 SBI | 特权级隔离执行权限，SBI 为内核提供固件服务；本次是 S 模式内核请求固件，不是用户进程向内核发系统调用 |
| 链接地址与 ELF 入口 | 链接器安排节和符号，加载器准备内存，控制流最终进入入口；链接布局本身不会启动 CPU，也不等于已建立虚拟内存 |
| 栈与调用约定 | 栈支持 C 执行上下文，`sp` 管栈位置，`ra` 保存普通调用返回地址；这里只建立启动栈，没有进程内核栈切换 |
| 伪指令与机器指令 | `la`、`tail` 是汇编层表达，最终展开受重定位和松弛影响；须以当前反汇编核对实际指令 |
| BSS 与零初始化 | 语言层零初始化依赖运行环境准备内存；本内核自行清零，不能只依赖镜像包含零字节 |
| 分层设备输出 | 格式化、控制台接口、固件服务分工；本实验借用固件输出，没有完整 UART 驱动或中断式 I/O |
| 调试与可观测性 | 断点、单步、寄存器及内存检查把静态代码与执行状态对应；入口命中不能证明固件每条初始化指令都已审计 |

OS 原理中重要但当前内核没有实现的内容包括：物理页分配、页表与虚拟内存、缺页异常、用户态进程及系统调用、进程/线程调度与上下文切换、同步互斥、时钟中断及完整异常处理、文件系统与块设备管理、多核启动和独立栈、完整设备驱动。页大小宏、SBI timer 包装或 Makefile 目录占位均不能视为这些功能已经实现。

## 11. 实验结论与未完成验证

实验记录与当前源码一致：构建产生 RISC-V 64 位 ELF 和原始镜像；Reset ROM 将控制权交给 OpenSBI，固件最终到达 `kern_entry`；入口把 `sp` 设置为 `0x80203000`，不改写 `ra` 地跳入 `kern_init`；启动文本成功输出，最终停留在 `0x8020003a` 的无限循环。原 loader 配置的下一阶段地址问题，在本环境中通过 `-kernel $(kernel)` 修复并获得运行和断点验证。

以下项目仍未验证，不影响两道正式练习已有的直接答案，但不能据此扩大结论：

- 未逐条跟踪 OpenSBI 内部初始化，未解析完整动态启动信息或设备树；未独立读取内核入口特权状态 CSR。
- 未给出完整 ELF 节/段表、BSS 边界和清零前后快照，也未逐字符统计输出换行。
- `libs/sbi.c::sbi_call` 直接改写 `a0/a1/a2/a7`，却仅声明 `memory` clobber，普通 `r` 输入约束存在潜在寄存器分配风险；本次输出成功，不能将风险写成已发生错误，仍需专门审计生成代码及约束。
- `sbi_console_getchar` 的实现缺失、`gp` 相关访问是否安全等旧笔记疑点未专项验证；成功链接和输出不代表未使用路径也可用。
- 工具具体版本、跨 QEMU 版本行为、课程仓库上传与最终提交状态需人工核对；本文不报告截图、性能指标或未执行测试。

本报告已覆盖文档全部正式练习和报告内容要求，保留原分析笔记与已有源码修改。课程提交及补充验证由实验者按实际需要完成。
