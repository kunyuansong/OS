# uCore Lab1 最小内核启动分析

本文整理自当前会话已完成的实际文件读取与静态分析，并结合 `lab1.docx` 核实练习要求。未编译、未运行 QEMU/GDB。下述源码行号对应本次读取版本；运行状态与最终机器指令仍待验证。

## 1. 项目结构与职责

- `kern/init/entry.S`：汇编入口、启动栈定义。
- `kern/init/init.c`：清零 BSS、打印信息、无限循环。
- `kern/mm/mmu.h`、`memlayout.h`：页大小与栈大小宏；当前没有内存管理实现。
- `kern/driver/console.c`：控制台字符接口。
- `kern/libs/stdio.c`：格式化输出入口与字符回调。
- `libs/printfmt.c`、`string.c`：格式解析及内存操作。
- `libs/sbi.c`：通过环境调用请求固件服务。
- `tools/kernel.ld`、`function.mk`、`Makefile`：链接布局与构建规则。

Makefile 第95—107行列出 `kern/debug`、`kern/trap` 等目录，但当前实际目录没有这些模块，不能认为异常处理、调试器或进程系统已经实现。

## 2. 完整启动链

```text
QEMU 加载 bin/ucore.img 到 0x80200000
→ 固件初始化并移交控制权
→ kern_entry：设置 sp，尾跳转
→ kern_init：清零 BSS
→ cprintf → vcprintf → vprintfmt → cputch
→ cons_putc → sbi_console_putchar → sbi_call → ecall
→ 固件控制台服务
→ 返回输出调用链，进入 while (1)
```

加载配置见 `Makefile` 第162—166行。固件实现不在仓库内；进入内核时的特权级、PC 和 hart 状态需要调试确认。实验文档给出的预期路径是 `0x1000`（MROM）→ `0x80000000`（OpenSBI）→ `0x80200000`（内核），不能当作本次实测结果。

## 3. 两个核心启动函数

### 3.1 kern_entry

`kern/init/entry.S` 第4—18行：

```asm
.section .text,"ax",%progbits
.globl kern_entry
kern_entry:
    la sp, bootstacktop
    tail kern_init
.section .data
    .align PGSHIFT
    .global bootstack
bootstack:
    .space KSTACKSIZE
    .global bootstacktop
bootstacktop:
```

`.text` 可分配、可执行，入口符号导出给链接器。第7行 `la` 将栈顶地址放入 `sp`，不是读取栈顶内容。它是伪指令，实际展开受重定位、链接松弛影响。

`kern/mm/mmu.h` 第4—5行定义 `PGSIZE=4096`、`PGSHIFT=12`；`memlayout.h` 第4—5行定义 `KSTACKPAGE=2`、`KSTACKSIZE=KSTACKPAGE*PGSIZE=8192`。`.align 12` 要求4096字节对齐，`.space` 预留8192字节。栈占据 `[bootstack, bootstacktop)`，初始 `sp` 在高地址端，按调用约定向低地址增长，也满足16字节栈对齐。

第9行 `tail kern_init` 转移控制权，不像普通 `call` 那样建立新的 `ra` 返回地址。其设计与初始化函数不返回一致。入口只设置栈，没有初始化 `gp`、页表、异常向量，也没有多核独立栈或参数保存。栈的绝对地址需链接后确认。

### 3.2 kern_init

`kern/init/init.c` 第4—14行：

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

`edata`、`end` 是链接器地址符号，声明不分配数组。第8行清零 `[edata,end)`，包括可能的 BSS 前对齐填充。启动栈位于 `.data`，在该范围之前，不会被清零。

`libs/string.c` 第270—279行的默认 `memset` 为：

```c
char *p = s;
while (n-- > 0) {
    *p++ = c;
}
return s;
```

项目未定义 `__HAVE_ARCH_MEMSET`，默认走此逐字节实现；Makefile 第31行使用 `-fno-builtin`。项目自己的接口第二参数为 `char`。

消息自带一个换行，格式串另加两个，所以输出字符序列以三个 `\n` 结束，不输出字符串终止符。第12—13行是忙循环，没有 `wfi` 或关机调用。`noreturn` 是编译器约定，实际不返回由循环保证。此内核没有用户进程退出环境，入口也未建立返回点。当前没有调用 `cons_init()`、内存分配器或中断初始化。

## 4. 链接布局与构建

`tools/kernel.ld` 的关键定义：

```ld
OUTPUT_ARCH(riscv)
ENTRY(kern_entry)
BASE_ADDRESS = 0x80200000;
```

第12—46行依次安排 `.text` → `etext` → `.rodata` → `ALIGN(0x1000)` → `.data` → `.sdata` → `edata` → `.bss`（含 `.sbss*`）→ `end`。第48—50行丢弃 `.eh_frame`、`.note.GNU-stack`。

`ENTRY` 设置 ELF 入口，不能主动改变 CPU 的 PC。脚本没有用 `AT(...)` 指定另一加载地址，意图使链接与加载地址一致。原始镜像没有 ELF 入口字段，实际跳转由固件完成。当前对象顺序使 `kern_entry` 预期位于 `0x80200000`，但最终入口、段地址、装载属性需检查 ELF。内核未建立地址映射，依赖固件提供可访问的地址环境。

构建规则见 `Makefile` 第84—140行、`tools/function.mk` 第6—28行：

```text
指定目录中的 .c/.S → GCC -c → obj/源路径.o
→ ld -T tools/kernel.ld → bin/kernel（ELF）
→ objcopy --strip-all -O binary → bin/ucore.img
```

例如入口目标为 `obj/kern/init/entry.o`；大写 `.S` 支持预处理宏。编译使用 `riscv64-unknown-elf-gcc`、`-mcmodel=medany -O2 -g -nostdinc`；链接使用 `elf64lriscv -nostdlib --gc-sections`。还生成 `.d` 依赖、`obj/kernel.asm` 和 `obj/kernel.sym`。BSS 通常不存入原始镜像，需运行时清零；具体段类型待查 ELF。

命令实际定义：

| 命令 | 行号及行为 |
|---|---|
| `make` | 第155—157行：默认 `TARGETS`，构建 ELF 和镜像 |
| `make qemu` | 第159—166行：`virt -nographic -bios default`，loader 加载镜像 |
| `make debug` | 第168—174行：追加 `-s -S`，提供1234端口并暂停 CPU |
| `make gdb` | 第176—180行：加载 `bin/kernel`，设置 RV64，连接1234端口 |

`gdb` 不自动构建或启动 QEMU。`SWAPIMG`、`SFSIMG` 未定义，默认为空，不能推定存在磁盘镜像。

## 5. SBI 字符输出机制

`kern/libs/stdio.c` 第37—43行：`cprintf` 用 `va_start` 组织参数，调用 `vcprintf`。第25—28行把 `cputch` 和计数器交给 `vprintfmt`。`libs/stdarg.h` 第5—9行使用编译器可变参数内建，`va_end` 为空宏。

`libs/printfmt.c` 第123—128行输出普通字符，第203—223行解析 `%s` 并逐字符回调。`stdio.c` 第11—13行的 `cputch` 调用 `cons_putc` 后增加计数；计数代表尝试输出数，不确认硬件发送成功。

`kern/driver/console.c` 第14行：

```c
void cons_putc(int c) { sbi_console_putchar((unsigned char)c); }
```

`libs/sbi.c` 第7、32—34行令服务号为1，并调用 `sbi_call(SBI_CONSOLE_PUTCHAR,ch,0,0)`。第16—29行的核心汇编为：

```asm
mv x17, %[sbi_type]
mv x10, %[arg0]
mv x11, %[arg1]
mv x12, %[arg2]
ecall
mv %[ret_val], x10
```

意图是 `a7` 放服务号，`a0` 放字符，`a1/a2` 为零，返回值从 `a0` 读取，属于 legacy SBI 接口形态。内核不直接写 UART；S 模式通过环境调用请求 M 模式固件服务。实际固件支持及串口发送未验证。

## 6. 正式练习与报告要求

`lab1.docx` 的练习1要求解释 `la sp,bootstacktop`、`tail kern_init` 的操作和目的，上述静态分析已覆盖。

练习2要求用 GDB 跟踪从加电到内核第一条指令，回答最初指令地址及其功能，并记录过程和观察。仅断在 `kern_entry` 不足以回答复位指令功能，还需反汇编并单步检查复位处代码。此部分尚未实测。

报告须使用 Markdown，说明整体逻辑、核心模块、全部练习答案、实验与 OS 原理知识的对应及差异，以及重要但本实验未涉及的知识，如进程调度、虚拟内存、文件系统。

## 7. 潜在问题与文档差异

- SBI 汇编仅声明 `memory` clobber，却直接改写 `a0/a1/a2/a7`，输入用普通 `r` 约束；可能发生寄存器重叠或优化错误，须检查反汇编。
- `sbi_console_getchar` 只有声明和调用，没有定义；未用输入路径可能被段回收，保留后可能链接失败。
- 未初始化 `gp`、未显式定义 `__global_pointer$`；是否产生依赖 `gp` 的访问待验证。
- 文档把入口段单独优先收集，实际脚本第15行合并匹配，入口仍在普通 `.text`，须核对最终地址。
- `tools/grade.sh` 不存在；`make grade` 还会先清理，不能认为可用。GDB 规则硬编码工具名，不随 `GCCPREFIX` 改变。
- `defs.h` 第52行的32位指针注释与第56—60行64位地址类型不一致。
- 文档部分段落称 OpenSBI 加载内核，后文又说 QEMU 预加载；当前 loader 配置支持后者。连接 GDB 后设置 `watch *0x80200000` 未必捕获已经完成的加载。
- 文档目录树包含当前不存在的模块；`ALIGN(0x1000)` 是4096字节对齐，不是“2的0x1000次方”。文档的“结束”在源码中是无限循环。

已确认的实现是设置栈、清零、输出、循环；固件交接、最终地址、指令展开及 SBI 正常输出仍需编译与 GDB 验证，不能用文档示例替代实测。
