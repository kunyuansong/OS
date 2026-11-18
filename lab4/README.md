# Lab4 代码说明

本工程基于所提供的 lab4.zip，合并已完成 lab3.zip 中必要的 lab2 和 lab3 实现，并补齐 lab4 的三个功能部分。无需再提供更早的实验代码。

## 第一部分 页表与内存映射

对应 `kern/mm/pmm.c` 的 `LAB4 function1`，实现 `page_remove_pte`、`page_remove`、`page_insert` 和 `pgdir_alloc_page`。映射同一物理页不会重复增加引用或提前释放；替换映射时释放旧引用；移除不存在的映射不产生副作用；修改有效映射后刷新 TLB。

本部分也完成运行所需的页表配套处理。`get_pte` 按 Sv39 三级结构建立页表，非叶子项只设置 V；新建中间页表失败时回收此次分配的页表页。对已有大页返回 NULL，不把大页误当成下一级页表。叶子项按权限设置 A/D，`perm=0` 解释为只读映射，写权限同时包含读权限，以避免形成无效的 Sv39 叶子项。

在 `pmm_init` 中，先用启动页表完成基础检查，再新建 4 KiB 粒度页表：内核代码段为 RX，只读数据段为 R，数据、BSS 和剩余可管理内存为 RW，均不设置 U。`tools/kernel.ld` 对段边界按页对齐。`check_boot_pgdir` 现在遍历真实的物理地址范围并检查段权限。

依赖来自 lab3：在 `default_pmm.c` 中接入已经完成的 best-fit 算法，通过原有 `default_pmm_manager` 接口导出；原骨架中的 `basic_check` 和 `best_fit_check` 保持原样。`page_init` 按 lab4 的 `sizeof(struct Page)` 重新计算描述符占用，保留固件、内核和描述符页面，注册剩余空闲内存。

## 第二部分 内核线程创建

对应 `kern/process/proc.c` 的 `LAB4 function2`，实现 `alloc_proc`、`copy_mm`、`copy_thread`、`do_fork`、`kernel_thread` 和 `proc_init`。

PCB 初始化后处于 PROC_UNINIT，pid 为 -1，页表字段保存内核根页表的物理字节地址。idle 线程继承启动栈，pid 为 0；init 线程分配独立的两页内核栈，pid 为 1，父线程为 idle。两个线程共享内核地址空间，mm 均为 NULL。

新线程的中断帧位于其内核栈顶部。子线程 a0 为 0，恢复后的 sp 指向内核栈顶；context 的 ra 指向 forkret，context 的 sp 指向中断帧。首次恢复通过 forkrets 和 sret 进入 kernel_thread_entry，使用 s0 中的函数和 s1 中的参数开始执行。线程只有完成初始化后才加入进程链表、PID 哈希表并变为可运行。PCB 或内核栈分配失败时释放已取得的资源。

原 `set_proc_name` 改用有长度限制的字符串复制，避免从短字符串固定读取 15 个字节。

## 第三部分 线程切换与调度

对应 `proc.c`、`kern/process/switch.S` 和 `kern/schedule/sched.c` 中的 `LAB4 function3`，实现 `proc_run`、`cpu_idle`、`switch_to` 和 `schedule`。

调度器从当前普通线程之后开始循环查找 PROC_RUNNABLE 线程；从 idle 开始时从链表头查找。新线程按创建顺序加入链表尾部；没有可运行线程时选择 idle。这里是协作式轮转，不增加时钟抢占。调度过程清除 need_resched，更新选中线程的 runs，并只在目标不同时切换。

切换时屏蔽中断，更新 current，把目标线程页表的物理地址编码到 satp，刷新 TLB，然后保存和恢复 ra、sp、s0 至 s11。寄存器偏移与 `struct context` 的字段顺序一致。线程以后被切回时，沿保存的返回地址继续运行，并恢复该次切换前的中断状态。

为支持以上流程，合并 lab3 的 trap 恢复实现并保留 lab4 新增的 forkrets；沿用 lab3 的分页过渡恒等映射，进入高地址后撤销它；固定链接入口顺序，并让运行、调试和评分命令统一使用 QEMU 的 `-kernel` 参数。未引入用户态进程、缺页分配、换页或退出回收。原骨架 `do_exit` 的 `process exit!!` panic 保留，init 输出结束后进入内核监视器是本阶段的预期路径。
