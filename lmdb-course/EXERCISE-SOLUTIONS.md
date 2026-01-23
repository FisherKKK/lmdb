# LMDB 课程练习题参考答案

本文档提供了各天练习题的参考答案和解析，帮助你验证自己的理解。

---

## Day 1 - LMDB 概述

### 思考题参考答案

**Q1: LMDB 为什么选择单写入者模型？**

*参考答案：*
- **优点**：
  1. 简化实现：避免复杂的锁竞争和死锁
  2. 性能优势：批量写入，减少磁盘同步
  3. MVCC 兼容：多个写事务需要复杂的版本协调
  4. 实际够用：写操作通常很快（毫秒级）

- **缺点**：
  1. 写入吞吐量受限于单个写事务
  2. 写入密集型应用可能成为瓶颈

**Q2: 内存映射如何实现"零拷贝"？**

*参考答案：*
```
传统 I/O:
  磁盘 -> 内核缓存 -> 用户缓存 -> 应用
  (多次拷贝)

内存映射:
  磁盘 -> 内核缓存（mmap 映射）
  应用直接访问内核缓存（无需拷贝）

关键点：
1. 数据不需要从内核空间复制到用户空间
2. 应用直接通过内存地址访问数据
3. 操作系统按需分页加载数据
```

**Q3: MVCC 是如何让读写不阻塞的？**

*参考答案：*
```
写事务修改数据时：
1. 复制页面到新位置（写时复制）
2. 原页面保持不变
3. 读事务继续读取原页面

读事务：
1. 记录快照 ID（事务开始时）
2. 总是读取快照 ID 对应的版本
3. 新版本对读事务不可见

结果：读写完全并发
```

**Q4: 为什么 LMDB 不需要像其他数据库那样的维护操作？**

*参考答案：*
- 写时复制保证数据不会被覆盖
- 元数据页交替更新，自动恢复
- 空闲列表自动管理
- 无需日志检查点或压缩
- 无需后台进程

---

## Day 2 - 内存映射 I/O

### 思考题参考答案

**Q1: mmap 相比传统 read/write 有什么优势？**

*参考答案：*
```
优势：
1. 零拷贝：减少数据复制
2. 自动分页：操作系统按需加载
3. 缓存管理：操作系统管理页缓存
4. 简化代码：无需手动管理缓冲区

劣势：
1. 映射大小固定（需要预先设置）
2. 文件大小限制
3. 信号处理更复杂
```

**Q2: 为什么需要预先设置映射大小？**

*参考答案：*
- mmap 需要在调用时指定映射大小
- 运行时扩展需要重新映射
- 重新映射可能改变地址，破坏指针

```c
// 正确的做法
mdb_env_set_mapsize(env, estimated_max_size);

// 错误的做法：不设置或设置过小
```

**Q3: WRITEMAP 模式有什么风险？**

*参考答案：*
```
风险：
1. 可能导致数据损坏（进程崩溃时）
2. 违反事务隔离性
3. 读操作可能看到未提交的数据

适用场景：
1. 只读数据库
2. 可以接受数据丢失的场景
3. 最高性能要求

不适用场景：
1. 数据一致性要求高
2. 关键业务数据
```

---

## Day 3 - 数据库环境

### 思考题参考答案

**Q1: 为什么需要设置 maxdbs？**

*参考答案：*
- LMDB 支持多个数据库（DBI）
- 每个 DBI 需要独立的元数据空间
- maxdbs 限制了同时打开的数据库数量
- 影响：默认值（128）可能不够

**Q2: 读者表满了会发生什么？**

*参考答案：*
```c
// 返回 MDB_READERS_FULL 错误
int rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
if (rc == MDB_READERS_FULL) {
    // 解决方案：
    // 1. 增加最大读事务数
    mdb_env_set_maxreaders(env, 256);

    // 2. 检查是否有泄漏的读事务
    //    确保每个读事务都正确中止

    // 3. 使用连接池复用读事务
}
```

**Q3: 如何检查数据库健康状态？**

*参考答案：*
```c
// 1. 检查环境信息
MDB_envinfo info;
mdb_env_info(env, &info);
printf("使用率: %.2f%%\n",
       100.0 * (info.me_last_pgno + 1) /
       (info.me_last_pgno + 1 + info.me_numfree_pages));

// 2. 检查统计信息
MDB_stat stat;
mdb_stat(txn, dbi, &stat);
printf("树深度: %u\n", stat.ms_depth);
printf("溢出页: %zu\n", stat.ms_overflow_pages);

// 3. 检查读者表
// 使用 mdb_reader_check() 清理过期读者
```

---

## Day 4 - 页面结构

### 思考题参考答案

**Q1: 为什么 mp_lower 和 mp_upper 从两端向中间增长？**

*参考答案：*
```
页面布局：
┌────────────────────────────────┐
│  Page Header                    │
├────────────────────────────────┤
│  Node Pointers (从低地址增长)    │
│                                 │
│           (空闲空间)             │
│                                 │
│  Node Data (从高地址增长)        │
└────────────────────────────────┘

原因：
1. 节点指针固定大小，从低地址增长
2. 节点数据变长，从高地址增长
3. 可以快速判断页面是否已满
4. 指针和数据在中间相遇
```

**Q2: 溢出页是如何存储大值的？**

*参考答案：*
```
溢出页结构：
[元数据页]
  └─ [节点] -> 键="large_data"
               数据=溢出页号 50
                   │
                   ▼
[溢出页链]
  [Page 50] -> [Page 51] -> [Page 52]

特点：
1. 按序号链接
2. 按需分配（OVPAGES 计算数量）
3. 读取时重组
4. 写时复制整个链
```

---

## Day 5 - B+树实现

### 思考题参考答案

**Q1: 为什么分支页从索引 1 开始搜索？**

*参考答案：*
```
分支页结构：
索引 0: 小于所有键的子页面
索引 1: [键1] -> [键1, 键2) 的子页面
索引 2: [键2] -> [键2, 键3) 的子页面
索引 3: [键3] -> 大于等于键3的子页面

原因：
1. 索引 0 存储"最小键"的子页面指针
2. 其他索引存储 [键, 子页面] 对
3. 这样可以覆盖所有可能的键值范围
```

**Q2: 如何优化 B+树的性能？**

*参考答案：*
```c
// 1. 使用整数键优化
mdb_set_compare(txn, dbi, mdb_cmp_int);

// 2. 选择合适的键大小
// 最佳：8-32 字节

// 3. 批量插入（已排序）
// 使用 MDB_APPEND 标志

// 4. 避免频繁的随机删除
// 可能导致页面碎片

// 5. 定期检查统计信息
MDB_stat stat;
mdb_stat(txn, dbi, &stat);
if (stat.ms_depth > 5) {
    // 考虑优化或重建
}
```

---

## Day 6-7 - 事务管理

### 思考题参考答案

**Q1: 读事务为什么不需要锁就能读取数据？**

*参考答案：*
```
核心机制：MVCC + 写时复制

1. 读事务记录快照 ID
2. 只读取快照 ID 对应的页面版本
3. 写事务创建新版本，原版本保持不变
4. 操作系统保证页面映射的原子性

结果：
- 读事务看到的始终是一致快照
- 无需加锁阻止写操作
```

**Q2: 嵌套事务的脏页如何合并到父事务？**

*参考答案：*
```c
// 合并过程

// 1. 合并空闲页列表
merge_idl(parent->mt_free_pgs, child->mt_free_pgs);

// 2. 更新父事务的下一个页号
parent->mt_next_pgno = child->mt_next_pgno;

// 3. 合并脏页列表
for (each dirty_page in child->mt_u.dirty_list) {
    add_to(parent->mt_u.dirty_list, dirty_page);
}

// 4. 更新数据库表
parent->mt_dbs = child->mt_dbs;
parent->mt_numdbs = child->mt_numdbs;

// 5. 清除父事务的阻塞标志
parent->mt_flags &= ~MDB_TXN_HAS_CHILD;
```

---

## Day 8 - MVCC

### 思考题参考答案

**Q1: MVCC 如何解决读写冲突？**

*参考答案：*
```
传统锁机制：
  读加锁 -> 写等待 -> 写加锁 -> 读等待

MVCC 机制：
  读 -> 记录快照 -> 读旧版本
  写 -> 创建新版本 -> 不阻塞读

关键：读写操作不同的数据版本
```

**Q2: 旧版本的数据页面何时可以回收？**

*参考答案：*
```c
// 回收条件

// 1. 页面的事务ID小于最旧的活跃读事务
if (page->mp_txnid < oldest_active_reader) {
    // 可以回收
}

// 2. 使用 mdb_page_unspill() 检查
for (each reader in reader_table) {
    if (reader->mr_txnid < page->mp_txnid) {
        // 该读事务可能还在使用此页面
        return 0; // 不能回收
    }
}

// 3. 事务提交时检查空闲列表
```

---

## Day 9-10 - 游标与锁

### 思考题参考答案

**Q1: 游标的页面栈有什么作用？**

*参考答案：*
```
作用：
1. 记录从根到当前节点的路径
2. 支持向上回溯（移动到父节点）
3. 支持范围查询（遍历）
4. 支持树操作（插入、删除）

B+树遍历示例：
Root (depth=2)
  └─ Branch (depth=1)
      └─ Leaf (depth=0) ← 当前

栈状态：
mc_pg[0] = Root
mc_pg[1] = Branch
mc_pg[2] = Leaf  ← mc_top
```

**Q2: 健壮锁如何避免死锁？**

*参考答案：*
```c
// 健壮锁特性
pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);

// 工作原理：
// 1. 持有锁的进程崩溃
// 2. 下一个获取锁的线程返回 EOWNERDEAD
// 3. 线程检测到状态，清理并继续

// LMDB 使用
if (lock_ret == EOWNERDEAD) {
    // 清理可能的不一致状态
    // 恢复到一致性状态
    // 继续操作
}
```

---

## Day 11-12 - 写操作与空间管理

### 思考题参考答案

**Q1: 写时复制有什么优势？**

*参考答案：*
```
优势：
1. 保护读事务快照
2. 原子性更新
3. MVCC 并发支持
4. 简单的崩溃恢复
5. 无需复杂的锁定机制

代价：
1. 空间开销（多版本）
2. 写放大（每次修改都复制）
3. 需要垃圾回收
```

**Q2: 如何避免空闲列表无限增长？**

*参考答案：*
```c
// 定期合并和压缩空闲列表
void mdb_freelist_merge(MDB_txn *txn) {
    // 1. 排序
    sort(txn->mt_free_pgs);

    // 2. 合并连续页号
    coalesce_continuous(txn->mt_free_pgs);

    // 3. 去除重复
    remove_duplicates(txn->mt_free_pgs);

    // 4. 持久化到 Free DB
    mdb_freelist_save(txn);
}
```

---

## 通用编程练习参考答案

### 练习 1: 实现一个简单的 KV 存储

```c
#include <lmdb.h>
#include <stdio.h>
#include <string.h>

int main() {
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;
    char cmd[16], k[64], v[64];
    int rc;

    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024 * 100);
    mdb_env_open(env, "./kvstore", 0, 0664);

    while (1) {
        printf("> ");
        scanf("%s", cmd);

        if (strcmp(cmd, "PUT") == 0) {
            scanf("%s %s", k, v);
            mdb_txn_begin(env, NULL, 0, &txn);
            mdb_dbi_open(txn, NULL, 0, &dbi);
            key.mv_data = k; key.mv_size = strlen(k);
            data.mv_data = v; data.mv_size = strlen(v);
            rc = mdb_put(txn, dbi, &key, &data, 0);
            mdb_txn_commit(txn);
            printf("OK\n");
        }
        else if (strcmp(cmd, "GET") == 0) {
            scanf("%s", k);
            mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
            mdb_dbi_open(txn, NULL, 0, &dbi);
            key.mv_data = k; key.mv_size = strlen(k);
            rc = mdb_get(txn, dbi, &key, &data);
            if (rc == 0) {
                printf("%.*s\n", (int)data.mv_size, (char *)data.mv_data);
            } else {
                printf("NOT FOUND\n");
            }
            mdb_txn_abort(txn);
        }
        else if (strcmp(cmd, "DEL") == 0) {
            scanf("%s", k);
            mdb_txn_begin(env, NULL, 0, &txn);
            mdb_dbi_open(txn, NULL, 0, &dbi);
            key.mv_data = k; key.mv_size = strlen(k);
            mdb_del(txn, dbi, &key, NULL);
            mdb_txn_commit(txn);
            printf("OK\n");
        }
        else if (strcmp(cmd, "QUIT") == 0) {
            break;
        }
    }

    mdb_env_close(env);
    return 0;
}
```

---

## 调试练习参考答案

### 练习: 调试事务提交问题

```bash
# 问题场景：事务提交失败

# 1. 使用 GDB 调试
gdb ./your_program

(gdb) break mdb_txn_commit
(gdb) run
(gdb) print rc
(gdb) if (rc != 0) print mdb_strerror(rc)
(gdb) step

# 2. 常见原因：
# - MDB_MAP_FULL: 映射空间满
#   解决：增加映射大小
#
# - MDB_TXN_FULL: 已有活跃写事务
#   解决：等待或检查是否忘记提交
#
# - EINVAL: 参数无效
#   解决：检查键值指针

# 3. 添加调试日志
#define DEBUG_LOG(fmt, ...) \
    fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)

DEBUG_LOG("Before mdb_txn_commit");
rc = mdb_txn_commit(txn);
DEBUG_LOG("After mdb_txn_commit: %s", mdb_strerror(rc));
```

---

## 性能优化练习参考答案

### 练习: 批量操作优化

```c
// 差的实现：每次操作都提交
void batch_operations_slow(MDB_env *env, int n) {
    for (int i = 0; i < n; i++) {
        MDB_txn *txn;
        mdb_txn_begin(env, NULL, 0, &txn);
        // ... 单个操作 ...
        mdb_txn_commit(txn);  // n 次提交！
    }
}

// 好的实现：批量提交
void batch_operations_fast(MDB_env *env, int n) {
    MDB_txn *txn;
    mdb_txn_begin(env, NULL, 0, &txn);

    // 所有操作在一个事务中
    for (int i = 0; i < n; i++) {
        // ... 操作 ...
    }

    mdb_txn_commit(txn);  // 只提交一次
}

// 性能提升：10-100 倍
```

---

## 总结

这些参考答案提供了：
1. 概念解释的要点
2. 代码实现的示例
3. 常见问题的解决方案
4. 调试技巧的演示

记住：
- 理解原理比记忆代码更重要
- 动手实践是最好的学习方式
- 遇到问题时先查看官方文档
- 源代码是最好的参考资料

祝学习愉快！
