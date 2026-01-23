# LMDB 底层实现 14天课程 - Day 8

## MVCC 与版本管理 - 读写不阻塞的秘密

欢迎回来！今天我们将深入 LMDB 最神奇的部分 - **MVCC（多版本并发控制）**。这是 LMDB 实现读写不阻塞的核心机制。

---

## 今天的目标

1. 理解 MVCC 的原理
2. 掌握 LMDB 的版本管理机制
3. 理解读事务的一致性视图
4. 学习写事务的隔离性

---

## 8.1 MVCC 概述

### 什么是 MVCC？

```
MVCC (Multi-Version Concurrency Control)：
  • 多版本：同时维护数据的多个版本
  • 并发控制：协调多个事务的访问
  • 核心思想：读不阻塞写，写不阻塞读
```

### 传统锁 vs MVCC

```
传统锁机制：
┌──────────┐    读锁     ┌──────────┐
│ 读事务 1 │ ────────> │   数据   │
└──────────┘           └──────────┘
                              │
                         写锁  │ 阻塞！
                              ▼
                        ┌──────────┐
                        │ 写事务 2 │
                        └──────────┘

MVCC：
┌──────────┐              ┌──────────┐
│ 读事务 1 │ ──────────> │ 版本 A   │ (旧版本)
└──────────┘              └──────────┘
                              │
                              │ 写入新版本
                              ▼
                        ┌──────────┐
                        │ 写事务 2 │ ─> │ 版本 B   │ (新版本)
                        └──────────┘     └──────────┘
```

---

## 8.2 LMDB 的 MVCC 实现

### 核心机制

```
LMDB MVCC 的三个关键要素：

1. 事务ID (txnid)
   • 每个写事务有唯一的递增ID
   • 读事务记录其开始时的txnid
   • 用于判断数据版本

2. 元数据页交替
   • 两个元数据页 (mm_txnid 奇偶交替)
   • 读事务看到固定的元数据页
   • 写事务创建新的元数据页

3. 写时复制 (Copy-on-Write)
   • 修改页面时创建副本
   • 旧版本保留给活跃的读事务
   • 读事务看到的是旧版本
```

### 版本链

```
页面版本历史：

初始状态 (txnid = 1):
  Page 5: [A, B, C]

写事务 2 (txnid = 2):
  Page 5: [A, X, C]  ← 新版本
  Page 5': [A, B, C] ← 旧版本（读事务 1 仍在使用）

写事务 3 (txnid = 3):
  Page 5: [A, Y, C]  ← 新版本
  Page 5': [A, X, C] ← 中间版本
  Page 5'': [A, B, C] ← 旧版本

读事务 1 开始于 txnid=1，看到 Page 5'
读事务 4 开始于 txnid=4，看到 Page 5
```

---

## 8.3 读事务的一致性视图

### 快照隔离

```c
// 读事务开始时捕获快照
int mdb_txn_begin(MDB_env *env, MDB_txn *parent,
                  unsigned int flags, MDB_txn **ret)
{
    // ...

    if (flags & MDB_RDONLY) {
        // 读事务：记录当前事务ID
        MDB_reader *r = /* 获取读者槽位 */;

        // 原子性地读取当前事务ID
        do {
            r->mr_txnid = ti->mti_txnid;
        } while (r->mr_txnid != ti->mti_txnid);

        // 选择对应的元数据页
        meta = env->me_metas[r->mr_txnid & 1];

        txn->mt_txnid = r->mr_txnid;
    }

    // ...
}
```

### 一致性保证

```
时间线：

t0: 系统状态
    txnid = 10
    Page 5: [A, B, C]

t1: 读事务 1 开始
    reader1.mr_txnid = 10
    看到 Page 5: [A, B, C]

t2: 写事务 11 开始
    修改 Page 5 -> Page 5': [A, X, C]
    提交，txnid = 11

t3: 读事务 1 继续读取
    仍然看到 Page 5: [A, B, C]
    （因为它读的是 txnid=10 时的版本）

t4: 读事务 2 开始
    reader2.mr_txnid = 11
    看到 Page 5': [A, X, C]
```

---

## 8.4 写事务的隔离性

### 单写入者保证

```c
// 获取写入者锁
LOCK_MUTEX(env->me_wmutex);

// 检查是否已有活跃写事务
if (env->me_txn) {
    UNLOCK_MUTEX(env->me_wmutex);
    return MDB_TXN_FULL;  // 同时只能有一个写事务
}

env->me_txn = txn;  // 设置为活跃写事务
```

### 可串行化隔离

```
LMDB 提供可串行化隔离级别：

特性：
  • 写事务看到的是已提交的数据
  • 写事务的修改对其他事务不可见
  • 直到提交，才对新的读事务可见
  • 同时只能有一个写事务

这保证了：
  • 无脏读 (Dirty Read)
  • 无不可重复读 (Non-Repeatable Read)
  • 无幻读 (Phantom Read)
```

---

## 8.5 页面版本管理

### 旧页面的生命周期

```
页面生命周期：

1. 创建 (新分配或复制)
   mdb_page_malloc()
   ↓
2. 使用 (写入数据)
   标记为 P_DIRTY
   ↓
3. 提交 (成为新版本)
   写入磁盘，清除 P_DIRTY
   ↓
4. 旧版本 (读事务不再引用)
   加入空闲列表
   ↓
5. 重用 (分配给新数据)
   从空闲列表取出
```

### 脏页列表

```c
// 写事务跟踪所有修改的页面
struct MDB_txn {
    // ...

    union {
        MDB_ID2L  dirty_list;  // 写事务：脏页列表
        MDB_reader *reader;    // 读事务：读者槽位
    } mt_u;

    // ...
};

// dirty_list 结构
// [0].mid = 脏页数量
// [1].mid = 页号, [1].mptr = 页面指针
// [2].mid = 页号, [2].mptr = 页面指针
// ...
```

---

## 8.6 读者表的作用

### 检测活跃读事务

```c
// 判断页面是否可重用
static int mdb_page_unspill(MDB_txn *txn, MDB_page *mp)
{
    MDB_env *env = txn->mt_env;
    MDB_txninfo *ti = env->me_txns;
    pgno_t pgno = MP_PGNO(mp);

    // 检查是否有读事务正在使用旧版本
    for (unsigned i = 0; i < ti->mti_numreaders; i++) {
        MDB_reader *r = &ti->mti_readers[i];
        if (r->mr_pid && r->mr_txnid > 0) {
            // 这个读事务是否在我们的版本之前开始？
            if (r->mr_txnid < txn->mt_txnid) {
                // 是的，这个页面可能还在使用
                return 1;  // 不能重用
            }
        }
    }

    return 0;  // 可以重用
}
```

### 读者表维护

```c
// 清理过期读者
int mdb_reader_check(MDB_env *env, int *dead)
{
    MDB_txninfo *ti = env->me_txns;
    int count = 0;

    for (unsigned i = 0; i < ti->mti_numreaders; i++) {
        MDB_reader *r = &ti->mti_readers[i];

        if (r->mr_pid != 0) {
            // 检查进程是否还存在
            if (!reader_alive(r)) {
                // 清理过期读者
                r->mr_pid = 0;
                r->mr_txnid = 0;
                count++;
            }
        }
    }

    if (dead) *dead = count;
    return count > 0 ? 1 : 0;
}
```

---

## 8.7 MVCC 的优势与代价

### 优势

```
1. 读写不阻塞
   • 读事务不需要锁
   • 写事务不阻塞读事务
   • 高并发性能

2. 一致性读
   • 读事务看到一致的快照
   • 不会看到部分提交的数据

3. 无死锁
   • 读事务不持有锁
   • 写事务串行执行
   • 不可能死锁
```

### 代价

```
1. 空间开销
   • 需要维护多个版本
   • 旧页面不能立即重用

2. 清理复杂性
   • 需要追踪活跃读事务
   • 延迟垃圾回收

3. 写放大
   • 每次修改都创建新版本
   • 虽然是必要的
```

---

## 8.8 完整示例：MVCC 可视化演示

### mvcc_demo.c - MVCC 版本可视化工具

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lmdb.h>
#include <pthread.h>
#include <unistd.h>

// 页面版本跟踪
typedef struct {
    pgno_t pgno;
    txnid_t version;
    char data[64];
    int is_active;
} PageVersion;

static PageVersion page_history[1000];
static int history_count = 0;

// 记录页面版本
void record_page_version(pgno_t pgno, txnid_t version, const char *data) {
    if (history_count < 1000) {
        page_history[history_count].pgno = pgno;
        page_history[history_count].version = version;
        strncpy(page_history[history_count].data, data, 63);
        page_history[history_count].is_active = 1;
        history_count++;
    }
}

// 打印版本历史
void print_version_history(pgno_t pgno) {
    printf("\n========== 页面 %u 的版本历史 ==========\n", pgno);
    for (int i = 0; i < history_count; i++) {
        if (page_history[i].pgno == pgno) {
            printf("版本 %llu: %s %s\n",
                   (unsigned long long)page_history[i].version,
                   page_history[i].data,
                   page_history[i].is_active ? "[活跃]" : "[已废弃]");
        }
    }
    printf("========================================\n\n");
}

// 读取线程
void *reader_thread(void *arg) {
    MDB_env *env = arg;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;
    int thread_id = (int)(long)arg;
    int count = 0;

    while (count < 10) {
        if (mdb_txn_begin(env, NULL, MDB_RDONLY, &txn) == 0) {
            if (mdb_dbi_open(txn, NULL, 0, &dbi) == 0) {
                key.mv_data = "counter";
                key.mv_size = 7;

                if (mdb_get(txn, dbi, &key, &data) == 0) {
                    printf("[读线程 %d] 事务 %llu 看到: %.*s\n",
                           thread_id, txn->mt_txnid,
                           (int)data.mv_size, (char *)data.mv_data);
                }
                mdb_dbi_close(env, dbi);
            }
            mdb_txn_abort(txn);
            count++;
        }
        usleep(500000);  // 500ms
    }

    return NULL;
}

// 写入线程
void *writer_thread(void *arg) {
    MDB_env *env = arg;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;
    int counter = 0;
    char value[64];

    while (counter < 10) {
        if (mdb_txn_begin(env, NULL, 0, &txn) == 0) {
            if (mdb_dbi_open(txn, NULL, 0, &dbi) == 0) {
                snprintf(value, sizeof(value), "value-%d", counter);

                key.mv_data = "counter";
                key.mv_size = 7;
                data.mv_data = value;
                data.mv_size = strlen(value);

                mdb_put(txn, dbi, &key, &data, 0);

                printf("[写线程] 事务 %llu 写入: %s\n",
                       txn->mt_txnid, value);

                mdb_txn_commit(txn);
                mdb_dbi_close(env, dbi);
                counter++;
            }
        }
        usleep(300000);  // 300ms
    }

    return NULL;
}

int main() {
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;
    pthread_t readers[3], writer;
    int rc;

    // 创建环境
    rc = mdb_env_create(&env);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_create failed: %s\n", mdb_strerror(rc));
        return 1;
    }

    mdb_env_set_mapsize(env, 1024 * 1024 * 100);
    mdb_env_set_maxreaders(env, 10);

    system("rm -rf testdb_mvcc && mkdir -p testdb_mvcc");
    rc = mdb_env_open(env, "./testdb_mvcc", 0, 0664);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_open failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    // 初始化数据
    mdb_txn_begin(env, NULL, 0, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);
    key.mv_data = "counter";
    key.mv_size = 7;
    data.mv_data = "initial";
    data.mv_size = 7;
    mdb_put(txn, dbi, &key, &data, 0);
    mdb_txn_commit(txn);

    printf("\n========== MVCC 演示开始 ==========\n");
    printf("观察读写事务如何并发执行而不互相阻塞\n\n");

    // 启动写线程
    pthread_create(&writer, NULL, writer_thread, env);

    // 启动多个读线程
    for (int i = 0; i < 3; i++) {
        pthread_create(&readers[i], NULL, reader_thread, (void *)(long)(i + 1));
    }

    // 等待完成
    pthread_join(writer, NULL);
    for (int i = 0; i < 3; i++) {
        pthread_join(readers[i], NULL);
    }

    printf("\n========== MVCC 演示结束 ==========\n");
    printf("观察结果：\n");
    printf("1. 读事务看到的是启动时的快照\n");
    printf("2. 写事务创建新版本，不影响读事务\n");
    printf("3. 读写完全并发，无阻塞\n\n");

    mdb_dbi_close(env, dbi);
    mdb_env_close(env);

    return 0;
}
```

---

## 8.9 快照隔离深度解析

### 快照隔离的实现机制

```c
// 快照隔离的核心：事务ID的选择
static int mdb_txn_renew0(MDB_txn *txn) {
    MDB_env *env = txn->mt_env;
    MDB_txninfo *ti = env->me_txns;
    MDB_meta *meta;

    if (txn->mt_flags & MDB_TXN_RDONLY) {
        // 读事务：获取当前全局事务ID
        // 这个ID决定了读事务能看到哪个版本的数据

        // 原子读取当前事务ID
        do {
            txn->mt_txnid = ti->mti_txnid;
        } while (txn->mt_txnid != ti->mti_txnid);

        // 根据事务ID选择元数据页
        // 两个元数据页交替使用：meta[0] 和 meta[1]
        // 选择规则：txnid & 1
        meta = env->me_metas[txn->mt_txnid & 1];
    }

    return MDB_SUCCESS;
}
```

### 版本可见性规则

```
版本可见性判断：

对于读事务 R (txnid = T)，页面 P (version = V)：

1. V <= T：页面可见
   - 页面是在读事务开始之前创建的
   - 或者是读事务自己的修改

2. V > T：页面不可见
   - 页面是在读事务开始之后创建的
   - 属于"未来"的版本

3. 特殊情况：脏页
   - 写事务自己的修改总是可见
   - 即使 V > T（因为是本事务创建的）
```

---

## 8.10 实践：观察 MVCC

### 练习 2: 验证快照隔离

```c
void test_snapshot_isolation(MDB_env *env) {
    MDB_txn *txn1, *txn2, *txn3;
    MDB_val key, data;

    // txn1: 写入初始数据
    mdb_txn_begin(env, NULL, 0, &txn1);
    key = (MDB_val){"key", 3};
    data = (MDB_val){"value1", 6};
    mdb_put(txn1, dbi, &key, &data, 0);
    mdb_txn_commit(txn1);

    // txn2: 开始读事务
    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn2);
    mdb_get(txn2, dbi, &key, &data);
    printf("txn2 sees: %.*s\n", (int)data.mv_size, (char *)data.mv_data);

    // txn3: 修改数据
    mdb_txn_begin(env, NULL, 0, &txn3);
    data = (MDB_val){"value2", 6};
    mdb_put(txn3, dbi, &key, &data, 0);
    mdb_txn_commit(txn3);

    // txn2: 再次读取
    mdb_get(txn2, dbi, &key, &data);
    printf("txn2 still sees: %.*s\n", (int)data.mv_size, (char *)data.mv_data);

    mdb_txn_abort(txn2);
}
```

---

## 8.9 常见问题解答

### Q1: MVCC 如何解决读写冲突？

**A:** MVCC 通过维护多个数据版本来避免冲突：

```
传统锁机制：
  读操作加读锁 - 写操作等待
  写操作加写锁 - 读/写操作都等待

MVCC 机制：
  读操作读取旧版本 - 无需等待
  写操作创建新版本 - 读操作不受影响
  读写完全分离 - 无阻塞
```

### Q2: 为什么 LMDB 只能有一个写事务？

**A:** 单写入者设计的原因：

1. **简化实现**：避免写操作的复杂锁管理
2. **性能优化**：批量写入，减少磁盘同步
3. **MVCC 兼容**：多个写事务需要复杂的版本协调
4. **实际够用**：写操作通常很快（毫秒级）

### Q3: 读事务如何保证看到一致的快照？

**A:** 通过三个机制：

1. **事务ID快照**：读事务开始时记录当前事务ID
2. **元数据页选择**：根据事务ID选择对应的元数据页
3. **页面版本检查**：只读取该事务ID之前的版本

```c
// 读事务总是看到一致的状态
txnid_t snapshot_id = txn->mt_txnid;  // 快照ID
// 所有后续读取都使用这个ID选择页面版本
```

### Q4: 旧版本的数据页面何时可以回收？

**A:** 页面回收的条件：

```c
// 检查页面是否可以回收
int page_reclaimable(MDB_env *env, pgno_t pgno) {
    MDB_txninfo *ti = env->me_txns;
    txnid_t oldest_txn = MDB_PNL_OLDEST(txn->mt_spill_pgs);

    // 页面的事务ID必须小于最旧的活跃读事务
    if (page->mp_txnid < oldest_txn) {
        return 1;  // 可以回收
    }
    return 0;
}
```

---

## 8.10 思考题

1. MVCC 如何解决读写冲突？
2. 为什么 LMDB 只能有一个写事务？
3. 读事务如何保证看到一致的快照？
4. 旧版本的数据页面何时可以回收？
5. MVCC 相比传统锁机制的主要优势是什么？

---

## 明天预告

Day 9 将深入讲解游标实现。我们将学习：
- 游标的结构和作用
- 游标的创建和销毁
- 游标的移动操作
- 游标与事务的交互

游标是 LMDB 的数据访问接口！

---
**参考文献：**
- mdb.c: 3061-3150 (读事务注册)
- mdb.c: 830-850 (MDB_reader)
- lmdb.h: MDB_txn 标志文档
