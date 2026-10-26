# ucore Lab2：SLUB Challenge（教学简化版）

## 安装

把 zip 包中的 `kern/` 目录解压覆盖到你已有的 `~/riscv-lab/lab2_new/` 中。
只新增 `kern/mm/slub.c`、`kern/mm/slub.h`，并修改 `kern/init/init.c`。
**不修改**现有 `pmm.c`、Best-Fit、Buddy、Makefile 和评分脚本。

在项目目录执行：

```bash
make clean && make qemu
```

预期内核输出：`slub_check() succeeded!`

然后运行：

```bash
make grade
```

原本的 `make grade` 主要检查 `page_init` 和 Best-Fit；SLUB 自测以 `slub_check() succeeded!` 为准，**不包含在原有 20/20 中**。

## 实现原理

1. `kmalloc(50)` 选 64 字节缓存。
2. 某缓存没有空闲对象时，向底层 `alloc_pages(1)` 申请整页。
3. 把 4096 字节页除去页内管理头后，切分为固定大小对象。
4. 在空闲对象内存放下一个空闲对象的指针，组成单向空闲链表。
5. `kfree` 将对象放回空闲链表；如果整页都空闲，调用 `free_pages(1)` 回收。
6. 超过 2048 字节的请求，直接申请足够多的完整物理页，页首页预留管理头。

## 支持的情况

- 16 字节对齐，最小对象缓存大小 16 字节。
- 固定大小级别：16/32/64/128/256/512/1024/2048 字节。
- 更大请求通过连续多页实现。
- 判断对象所属页、已分配位图、简单重复释放检查、空页回收。
- SLUB 自测：大小边界、不同对象大小、释放后重用、跨页大对象、多页 slab、恢复全部物理页。

## 限制

这是完成 Lab2 两层架构要求的**SLUB-style 简化实现**，不是 Linux 生产版 SLUB：
不支持并发锁、多核 CPU 缓存、NUMA、复杂调优或错误恢复。`kmalloc(0)` 返回 NULL。
由于回收后会立即归还完全空闲的 slab 页面，其性能和复杂度不等于 Linux SLUB。

## GDB

```gdb
b kmalloc
b kfree
b slub_alloc_small
b slub_new_small
b slub_alloc_large
b slub_check
c
```

如源码被 `-O2` 优化，可能出现断点多处命中或单步跳行。

## 供实验报告记录的提示词示例

**生成实现 prompt：** 请在 ucore Lab2 现有 `alloc_pages()/free_pages()` 接口之上独立设计两层简化 SLUB 对象分配器，支持按大小分类的小对象分配、每页对象空闲链表、整页回收、较大对象多页分配和 16 字节对齐；不要修改已有 First-Fit/Best-Fit/Buddy 接口或评分逻辑。

**生成测试 prompt：** 请为上述 SLUB 编写内核自测，覆盖不同 size class、0 字节、16/17/2048/2049 字节边界、对象内容不互相破坏、释放重用、跨页大对象、分配超过单 slab 容量、全部释放后空闲物理页数量恢复。

## 验证状态

已进行本地宿主机模拟物理页的运行测试及 RISC-V 目标语法检查；**没有在用户的 Ubuntu/QEMU 环境实际运行**。在提交前应执行 `make qemu` 和 `make grade`，并检查运行结果。
