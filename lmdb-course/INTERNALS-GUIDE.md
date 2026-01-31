# LMDB 底层实现学习指南

本指南专注于 LMDB 的底层实现细节，帮助你深入理解 mdb.c 的核心机制。

## 目录

1. [数据结构](#数据结构)
2. [核心算法](#核心算法)
3. [源代码导航](#源代码导航)
4. [关键函数详解](#关键函数详解)
5. [内存布局](#内存布局)
6. [并发控制](#并发控制)
7. [学习路径](#学习路径)

---

## 数据结构

### MDB_env - 环境句柄

```c
struct MDB_env {
    int me_fd;              // 数据库文件描述符
    int me_lfd;             // 锁文件描述符
    void *me_map;           // 内存映射基址
    size_t me_mapsize;      // 映射大小

    MDB_txn *me_txn;        // 当前写事务
    MDB_txn *me_txn0;       // 只读事务链表

    pgno_t me_last_pg;      // 最后使用的页面号
    MDB_db me_dbs[2];       // 主数据库和自由数据库

    unsigned me_maxreaders; // 最大读者数
};
```

**关键点：**
- `me_map` 是整个数据库文件的内存映射
- 所有页面通过 `me_map + pgno * PAGESIZE` 访问
- `me_txn0` 是所有只读事务的链表头

### MDB_txn - 事务

```c
struct MDB_txn {
    MDB_txn *mt_parent;     // 父事务（嵌套）
    size_t mt_txnid;        // 事务 ID
    unsigned mt_flags;      // 标志（只读/读写）

    MDB_env *mt_env;        // 环境句柄
    MDB_db *mt_dbs;         // 数据库句柄数组

    pgno_t mt_next_pgno;    // 下一个可用页面
    MDB_page *mt_rpages;    // 释放的页面链表

    void *mt_u;             // 读者槽位（只读事务）
};
```

**关键点：**
- 读写事务：`mt_parent = NULL` 或父事务，`mt_u = NULL`
- 只读事务：`mt_parent = NULL`，`mt_u` 指向读者槽位
- 嵌套事务：`mt_parent` 指向父事务

### MDB_page - 页面

```
偏移    大小      字段
+0      2 bytes   mp_pgno      页面号
+2      2 bytes   mp_flags     页面类型
+4      2 bytes   mp_lower     下界
+6      2 bytes   mp_upper     上界
+8      4 bytes   mp_pages     溢出页数
+12     PAGESIZE-12 数据区
```

**页面类型：**
- `0x02` - 分支页（内部节点）
- `0x04` - 叶页（包含数据）
- `0x05` - 溢出页（大值）
- `0x06` - 元页（数据库元数据）

### MDB_node - 节点

```c
// 分支页节点
struct {
    uint16_t ksize;         // 键大小
    uint16_t vsize;         // 值大小 (= pgno)
    pgno_t pgno;            // 子页面号
    char key_data[];        // 键数据
};

// 叶页节点
struct {
    uint16_t ksize;         // 键大小
    uint16_t vsize;         // 值大小
    uint16_t flags;         // 标志 (F_BIGDATA)
    char key_data[];        // 键数据
    char value_data[];      // 值数据
};
```

---

## 核心算法

### B+ 树搜索

```
mdb_cursor_get(cursor, key, data, MDB_SET)
  │
  └─► mdb_page_search(root, key)
        │
        ├─► 从根页开始
        │
        ├─► while (是分支页) {
        │      node = mdb_node_search(page, key);
        │      page = mdb_page_get(node->pgno);
        │    }
        │
        └─► return 叶页中的节点
```

**复杂度：** O(log n)
- n 是键的数量
- 树深度决定最大比较次数

### 页面分裂

```
插入导致页满
  │
  ├─► 分配新页 new_page
  │
  ├─► 复制前半部分到 old_page
  │   复制后半部分到 new_page
  │
  ├─► 提升中间键到父页
  │
  ├─► 父页满？
  │   ├─ 是 → 递归分裂
  │   └─ 否 → 完成
  │
  └─► 根页分裂？
      ├─ 是 → 树高度 +1
      └─ 否 → 完成
```

**分裂策略：** 50/50 分裂
- 平衡树的高度
- 保证所有叶页同深度

### 写时复制 (COW)

```
修改页面 P
  │
  ├─► 分配新页 P'
  │
  ├─► 复制 P → P'
  │
  ├─► 在 P' 上修改
  │
  ├─► 更新父页指针: P → P'
  │
  └─► P 等待所有旧读者完成后释放
```

**关键点：**
- 旧页保持完整，活跃读者继续使用
- 新页包含最新数据，新读者使用
- MVCC 通过 COW 实现

---

## 源代码导航

### mdb.c 结构

```
行号          内容
────────────────────────────────────
1-500         头文件和宏定义
500-1500      数据结构定义
1500-2500     内存管理
2500-4000     页面操作
4000-6500     B+ 核心函数
6500-8000     事务管理
8000-9500     游标操作
9500-11000    工具函数
11000-11500   平台特定代码
```

### 关键函数位置

| 函数 | 位置 | 功能 |
|------|------|------|
| mdb_env_create | ~7000 | 创建环境 |
| mdb_txn_begin | ~7500 | 开始事务 |
| mdb_txn_commit | ~7700 | 提交事务 |
| mdb_page_get | ~3000 | 获取页面 |
| mdb_page_search | ~4500 | 搜索页面 |
| mdb_page_touch | ~3200 | 标记页面脏 |
| mdb_page_alloc | ~2800 | 分配页面 |
| mdb_node_search | ~4700 | 搜索节点 |
| mdb_cursor_get | ~9200 | 游标操作 |
| mdb_get | ~8500 | 读取数据 |
| mdb_put | ~8600 | 写入数据 |

---

## 关键函数详解

### mdb_page_get()

**功能：** 获取页面的内存指针

```c
MDB_page *mdb_page_get(MDB_txn *txn, pgno_t pgno) {
    // 1. 计算页面地址
    // 2. 检查页面号是否有效
    // 3. 返回 me_map + pgno * PAGESIZE
}
```

**源码位置：** ~行 3000

**关键点：**
- 直接计算内存地址，无分配
- 使用 `txn->mt_env->me_map` 作为基址
- O(1) 时间复杂度

### mdb_page_search()

**功能：** 搜索包含键的页面

```c
int mdb_page_search(MDB_cursor *mc, MDB_val *key) {
    // 1. 从根页开始
    // 2. while (是分支页) {
    // 3.     node = mdb_node_search(page, key);
    // 4.     压入路径栈
    // 5.     page = mdb_page_get(node->pgno);
    // 6. }
    // 7. return 叶页
}
```

**源码位置：** ~行 4500-4700

**关键点：**
- 维护路径栈（用于游标）
- 二分查找优化
- 树高度决定迭代次数

### mdb_page_touch()

**功能：** 写时复制标记

```c
int mdb_page_touch(MDB_txn *txn, MDB_page *mp) {
    // 1. 检查页面是否已脏
    // 2. 分配新页
    // 3. 复制页面内容
    // 4. 更新父页指针
    // 5. 旧页加入释放列表
}
```

**源码位置：** ~行 3200

**关键点：**
- 只在写操作时调用
- 检查 `mp->mp_flags & P_DIRTY`
- 触发 COW 链

### mdb_txn_commit()

**功能：** 提交事务

```c
int mdb_txn_commit(MDB_txn *txn) {
    // 1. 检查嵌套？
    // 2. 合并到父或继续提交
    // 3. 刷新脏页到磁盘
    // 4. 更新元数据页
    // 5. 通知等待的写入者
    // 6. 清理事务资源
}
```

**源码位置：** ~行 7700-7850

**关键步骤：**
1. 检查是否嵌套
2. 调用 `mdb_env_sync()`
3. 更新事务 ID
4. 交替写入元数据页
5. 释放事务

---

## 内存布局

### 数据库文件布局

```
Page 0: Meta (主元数据)
  ├─ mapsize
  ├─ last_pgno
  ├─ txn_id
  └─ db[0].root (主数据库根页)

Page 1: Meta (备份元数据)
  └─ 同上，交替更新

Page 2: Branch (根页)
  ├─ keys: [cherry, pear]
  └─ 子页指针

Page 3: Leaf
  └─ 数据: [apple, banana]

Page 4: Leaf
  └─ 数据: [cherry, date]

...
```

### 内存映射视图

```
进程虚拟地址空间
  │
  ├─► me_map (基址)
  │   │
  │   ├─► Page 0 (Meta)
  │   ├─► Page 1 (Meta)
  │   ├─► Page 2 (Branch)
  │   ├─► Page 3 (Leaf)
  │   └─► ...
  │
  └─► 访问: me_map + pgno * 4096
```

---

## 并发控制

### 读者表

```
结构 (在锁文件中):
  struct {
      pid_t mr_pid;         // 进程 ID
      pthread_t mr_tid;     // 线程 ID
      size_t mr_txnid;      // 事务 ID
      void *mr_txn;         // 事务指针
  }

用途:
  1. 跟踪活跃的只读事务
  2. 检测过期读者
  3. 确定页面何时可重用
```

### MVCC 版本管理

```
时间线示例:
  T100: 写事务提交 → 版本 1
  T105: 读事务 A 开始 (看到版本 1)
  T110: 写事务提交 → 版本 2
  T115: 读事务 B 开始 (看到版本 2)

页面状态:
  旧页 (版本 1): 读事务 A 仍在使用
  新页 (版本 2): 读事务 B 使用

页面重用条件:
  旧页可被重用当: T100 < min(活跃读者的 txn_id)
```

---

## 学习路径

### 第一阶段：基础理解 (1-2周)

**目标：** 理解 LMDB 的基本概念和数据结构

1. 阅读 lmdb.h 了解公共 API
2. 运行 `internals_demo` 了解内部结构
3. 阅读 mdb.c 前 1500 行（数据结构定义）
4. 运行示例程序，熟悉基本操作

**检查点：**
- [ ] 能画出 MDB_env 的结构图
- [ ] 理解页面布局 (mp_pgno, mp_flags, mp_lower, mp_upper)
- [ ] 知道分支页和叶页的区别

### 第二阶段：核心算法 (2-3周)

**目标：** 理解 B+ 树和页面操作

1. 研究 `mdb_page_search()` - B+ 树搜索
2. 研究 `mdb_node_search()` - 页面内搜索
3. 研究 `mdb_page_split()` - 页面分裂
4. 研究 `mdb_page_touch()` - 写时复制

**检查点：**
- [ ] 能手写 B+ 树搜索算法
- [ ] 理解页面分裂的触发条件
- [ ] 理解 COW 的实现机制

### 第三阶段：事务管理 (2-3周)

**目标：** 理解事务的生命周期和 MVCC

1. 研究 `mdb_txn_begin()` - 事务开始
2. 研究 `mdb_txn_commit()` - 事务提交
3. 研究读者表管理
4. 研究空闲列表管理

**检查点：**
- [ ] 理解事务 ID 的作用
- [ ] 知道读者表如何工作
- [ ] 理解页面重用的判断条件

### 第四阶段：高级主题 (3-4周)

**目标：** 深入理解平台特定代码和优化

1. 研究 Windows vs Unix 的实现差异
2. 研究内存映射的实现
3. 研究锁机制
4. 研究性能优化技巧

**检查点：**
- [ ] 理解不同平台的差异
- [ ] 知道性能瓶颈在哪里
- [ ] 能够进行性能调优

---

## 调试技巧

### 编译调试版本

```bash
cd libraries/liblmdb
make CFLAGS="-DMD_DEBUG=1 -g -O0"
```

### 使用 GDB

```bash
gdb ./your_program

(gdb) break mdb_txn_begin
(gdb) run
(gdb) print *txn
(gdb) print txn->mt_txnid
(gdb) call mdb_txn_id(txn)
```

### 使用 Valgrind

```bash
valgrind --leak-check=full --show-leak-kinds=all \
         ./your_program
```

### 调试宏

```c
// 在 mdb.c 中添加
#define DEBUG_PRINT(x) printf x

DEBUG_PRINT(("Page number: %u\n", mp->mp_pgno));
```

---

## 常见问题

### Q: 为什么 LMDB 这么快？

**A:** 关键优化：
1. **零拷贝** - 内存映射，数据直接返回
2. **无锁读取** - MVCC 允许无锁并发
3. **缓存友好** - B+ 树，局部性好
4. **写时复制** - 避免缓冲区复制

### Q: 数据库文件为什么只增不减？

**A:** 设计决策：
1. 页面重用是内部行为
2. 不立即归还空间给文件系统
3. 空间在未来重用
4. 简化实现，提高性能

### Q: 如何理解页面分配？

**A:** 三个关键：
1. **空闲列表** - 跟踪可重用页面
2. **事务 ID** - 决定何时可重用
3. **文件扩展** - 只在必要时扩展

### Q: MVCC 如何实现？

**A:** 三个机制：
1. **写时复制** - 创建新版本
2. **读者表** - 跟踪活跃读者
3. **版本链** - 保持多个版本

---

## 推荐资源

### 代码工具

1. **internals_demo.c** - 数据结构演示
2. **btree_impl.c** - B+ 树实现
3. **page_allocator.c** - 页面分配
4. **code_explorer.c** - 源码导航

### 外部资源

1. **LMDB 官方文档** - http://www.lmdb.tech/doc/
2. **mdb.c 源码** - 约 11,500 行
3. **lmdb.h** - API 文档
4. **mtest*.c** - 测试程序

### 学习工具

```bash
# 查看函数定义
grep -n "mdb_page_search" mdb.c

# 统计代码行数
wc -l mdb.c

# 查看宏定义
grep -n "#define.*PAGE" mdb.c

# 生成调用图
cscope -bkq -i mdb.c
```

---

## 总结

LMDB 是一个优雅的设计，核心概念：

1. **内存映射** - 所有操作通过内存映射
2. **B+ 树** - 高效的索引结构
3. **MVCC** - 无锁并发
4. **写时复制** - 版本管理
5. **页面管理** - 空间重用

理解这些概念，你就能掌握 LMDB 的精髓！

**下一步行动：**
1. 运行 `internals_demo` 程序
2. 阅读 mdb.c 的前 2000 行
3. 使用 GDB 单步跟踪 `mdb_get`
4. 实现一个简化版的 B+ 树

祝你学习愉快！🚀
