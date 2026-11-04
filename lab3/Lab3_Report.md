# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
|---|---|
| **实验名称** | Lab 3：断，都可以断（中断与异常处理） |
| **小组成员** | 2411241-宋坤远、2413121-林科融、2410949-何翔 |
| **实验日期** | 2026-11-04 |
| **实验项目** | RISC-V ucore，课程 `lab3.zip` 的 `lab3_new` 骨架 |
| **提交目录** | `lab3/`，沿用上一实验 `Lab2_Report.md` 的报告命名与组织方式 |

### 小组分工与记录口径

本次实验代码标记使用经确认的学号 `2410949`。成员名单沿用 Lab2，未确认的个人分工不作推定。Codex 参与本次代码补全、故障定位、验证和报告整理；第四节给出按课程四段式模板整理的可复现提示词，并非逐字的历史对话记录。

## 一、实验目的

1. 区分 RISC-V 的异步中断与同步异常，理解 S 态 Trap 的入口、分发和返回。
2. 根据现有 `SAVE_ALL` 和 `struct trapframe` 恢复通用寄存器及返回所需的 CSR，建立从入口汇编到 C 处理函数的调用关系。
3. 实现持续的监督态时钟中断，完成重设事件、计数、周期打印和关机。
4. 处理非法指令及断点，按指令实际长度推进返回地址，并验证原执行流能够继续运行。

## 二、实验环境

| 项目 | 实测环境 |
|---|---|
| 主机及构建环境 | Windows，WSL Ubuntu 26.04 LTS |
| 交叉工具链 | `riscv64-unknown-elf-gcc` 15.1.0，GNU Binutils 2.45 |
| 构建工具 | GNU Make 4.4.1 |
| 模拟器与固件 | QEMU 7.0.0，`virt`，默认 OpenSBI v1.0 |
| 目标 | RV64，单 hart，Sv39；QEMU 日志显示 timebase 10 MHz |
| 调试方式 | `riscv64-unknown-elf-gdb`，连接 QEMU 的 GDB stub |

`make` 生成带符号的 `bin/kernel` 和裸二进制 `bin/ucore.img`。QEMU 用 `-kernel bin/ucore.img` 装载镜像，OpenSBI 的下一跳为物理地址 `0x80200000`；GDB 用 ELF 中的符号定位进入分页后的高虚拟地址。链接基址为 `0xffffffffc0200000`，两种地址对应同一段内核代码。

## 三、实验整体逻辑分析

本实验在 Lab1 的启动和 SBI 服务、Lab2 的物理页管理及分页基础上，接通了 Trap 的完整生命周期：先建立硬件入口，再由汇编保存现场，交给 C 选择处理行为，最后恢复现场并返回。

```text
kern_entry：进入 Sv39 高地址映射并设置启动栈
  -> kern_init：清 BSS、读取 DTB、初始化物理页、设置 stvec
  -> clock_init：设置 sie.STIE，预约第一次时钟事件
  -> intr_enable：设置 sstatus.SIE
  -> S 态 Trap：硬件记录 sepc/scause/stval 等，跳到 __alltraps
  -> SAVE_ALL -> trap(tf) -> interrupt_handler / exception_handler
  -> __trapret -> RESTORE_ALL -> sret
```

`stvec` 设置为四字节对齐的 `__alltraps`，使用 Direct 模式。硬件保存控制状态，但不会替内核保存通用寄存器。`trap_dispatch` 使用 `scause` 的最高位区分中断和异常；分发中断时再去掉该标志位取得原因编号。Lab3 实测的 Trap 来自 S 态内核，`sret` 仍返回 S 态。`sbi_set_timer()` 和 `sbi_shutdown()` 通过 `ecall` 请求 M 态固件服务，与内核处理的 S 态 Trap 路径不同。

## 四、实验内容与实现

### 4.1 功能模块一：Trap 上下文恢复与入口

实现位置：[kern/trap/trapentry.S](kern/trap/trapentry.S)。接口及布局见 [kern/trap/trap.h](kern/trap/trap.h)。

#### 实现与规格

RV64 的 Trap frame 共 36 个 8 字节槽位，占 288 字节，保留调用 C 函数所需的 16 字节栈对齐。槽位 0–31 对应 `x0`–`x31`，槽位 32–35 分别是 `sstatus`、`sepc`、`stval` 和 `scause` 的快照。`SAVE_ALL` 先用 `sscratch` 暂存原 `sp`，再把它写入槽位 2。

`__alltraps` 保存后将帧地址放进 `a0`，以 `jal trap` 调用 `trap(struct trapframe *tf)`；C 函数返回即进入 `__trapret`。`RESTORE_ALL` 先用临时寄存器写回 `sstatus`、`sepc`，再恢复除 `x0` 和 `sp` 之外的整数寄存器。`x0` 恒为零；`sp` 必须最后从槽位 2 加载，否则会丢失后续访存的帧基址。`stval/scause` 是本次 Trap 的诊断信息，不是 `sret` 所需的返回状态。

恢复时使用帧中**经处理函数更新后的** `epc`。中断保持原返回位置；已处理的同步异常可指定下一条指令。`sret` 将 `sepc` 交给 PC，并按 `sstatus.SPP/SPIE` 恢复特权级和中断使能；不在汇编中把返回级别硬编码为 U 态。

#### 规范化四段式提示词

```text
[PROMPT]
在实际工程的 kern/trap/trapentry.S 中补全两个 lab3 function1 标记。
保留 SAVE_ALL、struct trapframe、trap() 签名和标记结构；学号为 2410949。

[RELY]
SAVE_ALL 使用 36 * REGBYTES 栈空间；槽位 n 对应 xn，槽位 2 是原 sp。
槽位 32–35 依次对应 status、epc、badvaddr、cause。
LOAD/REGBYTES 由 riscv.h 定义；RISC-V ABI 的第一个整数或指针参数放入 a0。

[GUARANTEE]
实现 RESTORE_ALL 和 __alltraps 中从保存现场到调用 void trap(struct trapframe *tf) 的步骤。
trap() 返回后沿现有 __trapret 和 sret 路径继续。

[SPECIFICATION]
__alltraps 前置：处理器已进入 S 态 Trap 入口，当前内核栈有效。
后置：trap() 收到完整帧地址，并可在返回前改写帧的返回状态。
RESTORE_ALL 前置：sp 指向与 SAVE_ALL 匹配的帧，C 函数已返回。
后置：sstatus、sepc 和原整数执行现场恢复；以帧中的新 epc 为准。
约束：先读取恢复所需的槽位，再恢复 sp；x0 不需要写回。
验收：真实断点可返回，寄存器检查和前后 sp 对比通过。
```

这样写规格，是为了把“恢复现场”变成可核对的槽位和执行条件。若只说“恢复全部寄存器”，实现容易提前改写 `sp`，或错误地从旧的 `sepc` 恢复。

### 4.2 功能模块二：中断与异常处理

实现位置：[kern/trap/trap.c](kern/trap/trap.c)，复用 [kern/driver/clock.c](kern/driver/clock.c) 和 [libs/sbi.c](libs/sbi.c)。

#### 实现与规格

- **监督态定时器**：每次先调用 `clock_set_next_event()` 重新预约，然后仅增加一次 `ticks`。每到 `TICK_NUM=100` 的倍数，用原有 `print_ticks()` 输出 `100 ticks`。指导书引用 `SHUTDOWN_AFTER`，但提供的网页和骨架未给出数值；本实现取 `10`，所以第 1000 次中断打印第十行后调用 `sbi_shutdown()`。
- **其他中断**：按骨架给出的软件、定时器、外部中断编号给出提示；未知编号先打印原因号，再输出完整 Trap frame。实验没有为这些中断接入设备驱动或通用应答逻辑。
- **非法指令和断点**：分别输出 `Illegal instruction at` 或 `Breakpoint at`，附原指令地址。读取 `tf->epc` 指向的第一个半字，低两位不等于 `11` 时长度为 2，否则在本实验支持的 16/32 位编码范围内长度为 4。修改帧中的 `epc`，由汇编写回 `sepc`。当前内核代码映射可读；这个处理不能直接当作任意用户地址的安全取指接口。
- **其他异常**：用 `print_trapframe(tf)` 打印诊断，不盲目跳过指令。未实现的异常恢复逻辑不在本次范围内。

定时器用 `rdtime` 读取时间基准，并预约 `now + 100000`。在本次 10 MHz 平台上，间隔约为 10 ms，100 次约为一秒。每次从当前时间预约会受处理延迟影响，因此这不是精确无漂移的时钟实现。异步中断的返回地址不应加 2 或 4。

#### 规范化四段式提示词

```text
[PROMPT]
在实际工程 kern/trap/trap.c 的 lab3 function2 标记处实现分发后的两类处理。
保留现有 trap_dispatch、trap、print_ticks 和诊断函数；学号为 2410949。

[RELY]
struct trapframe 中的 cause 和 epc；riscv.h 中的 IRQ_* 和 CAUSE_*。
clock_set_next_event()、volatile ticks、TICK_NUM、print_ticks()、sbi_shutdown()。
未知类型使用 print_trapframe(tf)。当前目标的测试指令有 2 字节、4 字节两类。

[GUARANTEE]
void interrupt_handler(struct trapframe *tf);
void exception_handler(struct trapframe *tf);
允许增加文件内部的指令长度辅助函数与关机阈值常量。

[SPECIFICATION]
中断前置：trap_dispatch 已分类，cause 的最高位可能含中断标志。
时钟分支：重新预约下一次事件、计数一次，不改变 epc；每 100 次打印，
累计打印 10 次后调用框架的关机接口。其他已知中断输出类型，未知输出帧。
异常前置：tf->epc 指向可读的当前内核指令。
非法指令和断点：输出指定前缀和原地址，按第一半字的长度标志推进 epc。
其他异常：仅打印帧，不假定能够恢复。
验收：课程三个评分用例通过，混合 2/4 字节异常可返回，普通启动打印十行后关机。
```

这样写规格，是为了区分异步与同步事件的返回条件，并明确“打印次数”和“中断次数”的关系。若只要求“打印后关机”，可能在第 10 个 tick 就退出；若不明确长度，固定加 4 会跳过压缩指令后的正常代码。

### 4.3 实际迭代和故障处理

| 阶段 | 观察到的问题 | 定位及修复 |
|---|---|---|
| 原始骨架编译 | `interrupt_handler`、`exception_handler` 未声明 | 核对两处模块占位，补齐入口、恢复与处理函数后，编译继续推进。 |
| 汇编兼容 | Binutils 2.45 报 `unknown CSR 'sbadaddr'` | 在 `SAVE_ALL` 之外将旧名称映射到同一 CSR 的 `stval`，保留保存宏正文和帧布局。 |
| Lab2 前置代码 | Best-Fit 自检编译时报 `free_list`、`nr_free` 未定义；`page_init()` 同样留空 | 在两处 Lab2 标记复用仓库已有实现。内存界限从本工程 `KERNBASE/KERNTOP` 与偏移推导；保留 Lab3 的 `local_intr_save/restore` 包装和默认 First-Fit 选择。 |
| 固件交接 | `-device loader` 模式输出下一跳 `0x0`，未到内核 | 与仓库 Lab2 启动方式对齐：普通、调试、测试与评分均用 `-kernel`；评分用例及判分条件不变。 |
| 开启分页 | 下一跳已正确，但 QEMU `-d int` 报 `exec_page_fault`，首次 EPC 为 `0x80200038` | 开启 `satp` 时 PC 仍在低地址，原根页表只有高地址映射。启动根表临时加第 2 项覆盖过渡取指；跳到高地址后清除该项并执行 `sfence.vma`，再建立栈进入 C。 |

这些是实现、编译和启动过程中实际观察到的失败。提前恢复 `sp`、漏设定时器、固定步长跳过异常等是规格主动约束的风险，不是本次已出现的故障。

## 五、测试与验证

在 `lab3/` 中构建和运行：

```sh
make
make qemu
make grade
```

`make qemu` 实测通过内存自检，连续输出十行 `100 ticks` 后退出，退出状态为 0；未由测试超时终止。关键日志如下，其中重复行已省略：

```text
Domain0 Next Address      : 0x0000000080200000
Domain0 Next Mode         : S-mode
check_alloc_page() succeeded!
++ setup timer interrupts
100 ticks
```

课程评分脚本在临时副本注入测试，不向正式内核加入测试钩子。实测给 `make grade` 指定独占的 `GRADE_TMP`，三个用例全部通过，退出状态为 0：

| 用例 | 覆盖内容 | 结果 |
|---|---|---|
| 1 | 断点返回及测试覆盖的通用寄存器恢复 | 35/35 |
| 2 | 非法指令推进 EPC 并返回，软件中断、未知中断和异常诊断分支 | 25/25 |
| 3 | 重新预约监督态时钟、多次 Trap 返回及 `ticks=200` | 40/40 |
| **总分** | `Total Score: 100/100` | **100/100** |

GDB 补测在 `clock_init` 入口处暂停，只修改本次 QEMU 客体内存，在已经执行完毕的 `kern_init` 起始地址放入 `0x9002`、`0x00100073`、`0x0000`、`0x0000007b`，分别测试 2 字节断点、4 字节断点、2 字节非法指令、4 字节非法指令。四条诊断地址相对起点依次为 `+0`、`+2`、`+6`、`+8`，断在 `+12`，说明返回地址按各自长度推进。调试还核对了分页过渡映射已清除：

```text
Boot mapping: low PTE=0 high PTE=0x200000cf
Mixed-width traps: start=0xffffffffc0200060 end=0xffffffffc020006c
sp-before=0xffffffffc0205ff0 sp-after=0xffffffffc0205ff0
Mixed-width exception return: PASS
```

## 六、实验总结与收获

### 6.1 与操作系统原理的对应

| 知识点 | 本实验中的体现 | 联系与区别 |
|---|---|---|
| 中断、异常与入口 | `stvec`、`scause` 和 `trap_dispatch` | 中断通常独立于当前指令，异常由当前执行同步产生；两者共用入口，但处理动作和返回策略不同。 |
| 上下文保存与 ABI | `SAVE_ALL`、`RESTORE_ALL`、`a0`、`sret` | Trap 可能在任意指令边界发生，需恢复被打断的上下文；普通 C 函数调用只要求遵守 ABI 的寄存器保存规则。 |
| 特权和中断屏蔽 | `SPP`、`SPIE`、`SIE`、`sie.STIE` | Trap 进入及返回由硬件协助；全局使能与单一中断源使能是两层条件。OpenSBI 的 M 态服务不是用户系统调用。 |
| 时钟与调度 | 预约事件、累计 `ticks` | 周期性中断可为时间片调度提供时基；本实验只验证时钟和返回链，没有调度进程。 |
| 临界区与原子操作 | `local_intr_save/restore` 与页标志位操作 | 保存并恢复本 hart 原中断使能，避免同核中断打断临界区；它不能代替多 hart 的锁。`do { ... } while (0)` 使多语句宏在语法上表现为一条语句。 |
| 虚拟地址与页表切换 | 临时低地址映射、高地址入口、`sfence.vma` | 改写 `satp` 后仍需保证下一条指令可取到；修改映射后要同步地址转换状态。 |

### 6.2 本实验尚未覆盖的内容

没有实现用户态 Trap 的内核栈切换、用户系统调用和安全用户访存，也没有实现进程调度及切换、缺页补页、设备 IRQ 的完整应答流程、多 hart 中断与锁、浮点或向量寄存器现场管理。`print_trapframe` 提供的是故障诊断，不能代替这些处理策略。

## 参考资料

- [课程 Lab3 入口](http://oslab.mobisys.top/lab2026/_book/lab3/lab3.html)
- [Lab3 练习](http://oslab.mobisys.top/lab2026/_book/lab3/lab3_2_1_exercise.html)
- [Lab3 实验报告要求](http://oslab.mobisys.top/lab2026/_book/lab3/lab3_4_lab_requirement.html)
- [课程提示词结构](http://oslab.mobisys.top/lab2026/_book/lab0.5/3_prompt_structure.html)
- [本组 Lab2 报告](../lab2/Lab2_Report.md)
