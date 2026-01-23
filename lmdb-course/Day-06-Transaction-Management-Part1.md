# LMDB 底层实现 14天课程 - Day 6

## 事务管理（上）- 事务的生命周期

欢迎回来！今天我们进入 LMDB 最核心的部分之一 - **事务管理**。事务是 LMDB ACID 保证的基础，理解它对掌握整个系统至关重要。

---

## 今天的目标

1. 理解事务的结构和状态
2. 掌握读事务的实现
3. 理解写事务的实现
4. 学习嵌套事务

---

## 6.1 MDB_txn 结构体回顾

```c
struct MDB_txn {
    // === 层次结构 ===
    MDB_txn  *mt_parent;      // 父事务（嵌套事务）
    MDB_txn  *mt_child;       // 子事务

    // === 基本信息 ===
    txnid_t   mt_txnid;       // 事务ID
    MDB_env  *mt_env;         // 所属环境

    // === 页面管理 ===
    pgno_t    mt_next_pgno;   // 下一个未分配页号
    MDB_IDL   mt_free_pgs;    // 本事务释放的页面
    MDB_page *mt_loose_pgs;   // 可重用的空闲页面
    int       mt_loose_count; // 空闲页面数量

    // === 脏页管理 ===
    MDB_IDL   mt_spill_pgs;   // 溢出并写回磁盘的页面
    union {
        MDB_ID2L  dirty_list;  // 写事务：脏页列表
        MDB_reader *reader;    // 读事务：读者表槽位
    } mt_u;

    // === 数据库信息 ===
    MDB_dbx   *mt_dbxs;       // 数据库辅助信息数组
    MDB_db    *mt_dbs;        // 数据库记录数组
    unsigned int *mt_dbiseqs; // 数据库序列号数组
    MDB_cursor **mt_cursors;  // 游标数组（写事务）
    unsigned char *mt_dbflags;// 数据库标志数组

    // === 状态 ===
    MDB_dbi   mt_numdbs;      // 数据库数量
    unsigned int mt_flags;    // 事务标志
    unsigned int mt_dirty_room; // 脏页空间配额
};
```

---

## 6.2 事务标志

```c
// mdb_txn_begin() 可用标志
#define MDB_TXN_BEGIN_FLAGS  (MDB_NOMETASYNC | MDB_NOSYNC | MDB_RDONLY)
#define MDB_TXN_NOMETASYNC   MDB_NOMETASYNC  // 提交时不同步元数据
#define MDB_TXN_NOSYNC       MDB_NOSYNC      // 提交时不同步数据
#define MDB_TXN_RDONLY       MDB_RDONLY      // 只读事务

// 内部标志
#define MDB_TXN_WRITEMAP     MDB_WRITEMAP    // 写映射模式
#define MDB_TXN_FINISHED     0x01            // 事务已结束
#define MDB_TXN_ERROR        0x02            // 事务出错
#define MDB_TXN_DIRTY        0x04            // 必须写入
#define MDB_TXN_SPILLS       0x08            // 有溢出页
#define MDB_TXN_HAS_CHILD    0x10            // 有子事务
#define MDB_TXN_BLOCKED      (MDB_TXN_FINISHED | MDB_TXN_ERROR | MDB_TXN_HAS_CHILD)
```

---

## 6.3 事务开始 - mdb_txn_begin()

### 函数签名

```c
int mdb_txn_begin(MDB_env *env, MDB_txn *parent,
                  unsigned int flags, MDB_txn **ret);
```

### 流程图

```
mdb_txn_begin()
    │
    ├─> 1. 参数验证
    │       ├─ 检查 env 是否有效
    │       ├─ 检查 flags 是否合法
    │       └─ 检查父事务状态
    │
    ├─> 2. 分配事务结构
    │       ├─ 使用预分配的 env->me_txn0（对于顶层写事务）
    │       └─ 或分配新的 MDB_txn
    │
    ├─> 3. 初始化基本字段
    │       ├─ 设置 mt_env
    │       ├─ 设置 mt_parent
    │       └─ 设置 mt_flags
    │
    ├─> 4. 调用 mdb_txn_renew0()
    │       │
    │       ├─> 如果是读事务：
    │       │       ├─ 在读者表中注册
    │       │       ├─ 获取当前事务ID
    │       │       └─ 选择元数据页
    │       │
    │       └─> 如果是写事务：
    │               ├─ 获取写入者锁
    │               ├─ 分配新事务ID
    │               └─ 选择元数据页
    │
    ├─> 5. 初始化数据库信息
    │       ├─ 复制父事务的数据库信息（如果有）
    │       └─ 或从环境初始化
    │
    └─> 6. 返回事务句柄
```

### 核心代码 (mdb.c:3061)

```c
static int mdb_txn_renew0(MDB_txn *txn)
{
    MDB_env *env = txn->mt_env;
    MDB_txninfo *ti = env->me_txns;
    MDB_meta *meta;
    unsigned int i, nr;
    int rc;

    if (txn->mt_flags & MDB_TXN_RDONLY) {
        // === 读事务 ===
        if (!ti) {
            // 无锁模式（MDB_NOLOCK）
            meta = mdb_env_pick_meta(env);
            txn->mt_txnid = meta->mm_txnid;
        } else {
            // 在读者表中查找/分配槽位
            MDB_reader *r = pthread_getspecific(env->me_txkey);
            if (!r) {
                // 需要分配新槽位
                LOCK_MUTEX(env->me_rmutex);
                nr = ti->mti_numreaders;
                for (i = 0; i < nr; i++)
                    if (ti->mti_readers[i].mr_pid == 0)
                        break;
                if (i == env->me_maxreaders) {
                    UNLOCK_MUTEX(env->me_rmutex);
                    return MDB_READERS_FULL;  // 读者表满了
                }
                r = &ti->mti_readers[i];

                // 原子性地注册
                r->mr_pid = 0;
                r->mr_txnid = (txnid_t)-1;
                r->mr_tid = pthread_self();
                if (i == nr)
                    ti->mti_numreaders = ++nr;
                r->mr_pid = env->me_pid;
                UNLOCK_MUTEX(env->me_rmutex);

                pthread_setspecific(env->me_txkey, r);
            }

            // 获取当前事务ID（原子读取）
            do {
                r->mr_txnid = ti->mti_txnid;
            } while (r->mr_txnid != ti->mti_txnid);

            // 根据事务ID选择元数据页
            meta = env->me_metas[r->mr_txnid & 1];
            txn->mt_txnid = r->mr_txnid;
            txn->mt_u.reader = r;
        }

    } else {
        // === 写事务 ===
        if (ti) {
            // 获取写入者锁
            LOCK_MUTEX(env->me_wmutex);
            // 获取当前事务ID并加1
            txn->mt_txnid = ti->mti_txnid;
            meta = env->me_metas[txn->mt_txnid & 1];
        } else {
            // 无锁模式
            meta = mdb_env_pick_meta(env);
            txn->mt_txnid = meta->mm_txnid;
        }
        txn->mt_txnid++;  // 分配新的事务ID
    }

    // 设置元数据页指针
    env->me_metas[txn->mt_txnid & 1] = meta;

    return MDB_SUCCESS;
}
```

---

## 6.4 读事务详解

### 读事务特点

```
┌─────────────────────────────────────────────────────────────┐
│  读事务特点                                                 │
├─────────────────────────────────────────────────────────────┤
│  • 无需锁（除了读者表注册）                                  │
│  • 看到的是一致性的快照                                      │
│  • 多个读事务可以并发                                        │
│  • 读事务永不阻塞其他读事务                                  │
│  • 读事务不阻塞写事务                                        │
│  • 写事务不阻塞读事务（MVCC）                                │
└─────────────────────────────────────────────────────────────┘
```

### 读者表注册流程

```c
// 步骤 1: 尝试获取线程本地存储的读者槽位
MDB_reader *r = pthread_getspecific(env->me_txkey);

// 步骤 2: 如果没有，需要分配新槽位
if (!r) {
    LOCK_MUTEX(env->me_rmutex);

    // 查找空闲槽位
    for (i = 0; i < env->me_maxreaders; i++) {
        if (ti->mti_readers[i].mr_pid == 0)
            break;
    }

    // 检查是否超限
    if (i == env->me_maxreaders) {
        UNLOCK_MUTEX(env->me_rmutex);
        return MDB_READERS_FULL;
    }

    // 分配槽位
    r = &ti->mti_readers[i];
    r->mr_pid = env->me_pid;
    r->mr_tid = pthread_self();
    r->mr_txnid = ti->mti_txnid;  // 记录当前事务ID

    UNLOCK_MUTEX(env->me_rmutex);

    // 保存到线程本地存储
    pthread_setspecific(env->me_txkey, r);
}

// 步骤 3: 记录事务ID
txn->mt_txnid = r->mr_txnid;
```

### 读者表槽位状态

```
未使用：
  mr_pid = 0
  mr_txnid = 0
  mr_tid = 0

已注册：
  mr_pid = <进程ID>
  mr_tid = <线程ID>
  mr_txnid = <事务ID>

正在退出：
  mr_pid = <进程ID>
  mr_txnid = (txnid_t)-1  // 特殊值表示正在退出
```

---

## 6.5 写事务详解

### 写事务特点

```
┌─────────────────────────────────────────────────────────────┐
│  写事务特点                                                 │
├─────────────────────────────────────────────────────────────┤
│  • 同时只能有一个写事务                                      │
│  • 需要获取写入者锁                                          │
│  • 有唯一递增的事务ID                                        │
│  • 跟踪所有修改的页面（脏页列表）                             │
│  • 提交时原子性地更新元数据                                  │
└─────────────────────────────────────────────────────────────┘
```

### 写事务初始化

```c
// 获取写入者锁
LOCK_MUTEX(env->me_wmutex);

// 检查是否已有活跃写事务
if (env->me_txn) {
    UNLOCK_MUTEX(env->me_wmutex);
    return MDB_TXN_FULL;  // 同时只能有一个写事务
}

// 设置为活跃写事务
env->me_txn = txn;

// 分配新事务ID
txn->mt_txnid = ti->mti_txnid + 1;

// 初始化脏页列表
txn->mt_dirty_room = MDB_IDL_UM_SIZE;  // 脏页配额

// 初始化其他字段
txn->mt_next_pgno = meta->mm_last_pg + 1;
txn->mt_free_pgs = env->me_free_pgs;
```

---

## 6.6 事务ID

### 事务ID的用途

```
事务ID (txnid_t)：
  • 唯一标识每个提交的写事务
  • 用于选择元数据页 (txnid & 1)
  • 用于判断页面版本
  • 用于MVCC一致性检查
```

### 元数据页选择

```c
// 元数据页交替使用
MDB_meta *meta = env->me_metas[txnid & 1];

// 示例：
// txnid = 1  -> 使用 me_metas[1]
// txnid = 2  -> 使用 me_metas[0]
// txnid = 3  -> 使用 me_metas[1]
// txnid = 4  -> 使用 me_metas[0]
```

### 事务ID的生命周期

```
写事务 1: txnid = 1
  ├─ 修改页面
  ├─ 提交
  └─ 更新 me_metas[1].mm_txnid = 1

写事务 2: txnid = 2
  ├─ 修改页面
  ├─ 提交
  └─ 更新 me_metas[0].mm_txnid = 2

写事务 3: txnid = 3
  ├─ 修改页面
  ├─ 提交
  └─ 更新 me_metas[1].mm_txnid = 3

...以此类推
```

---

## 6.7 嵌套事务

### 嵌套事务结构

```
┌─────────────────────────────────────────────────────────────┐
│  顶层事务 (txnid = 10)                                       │
│  ├─ mt_parent = NULL                                        │
│  ├─ mt_child = 嵌套事务                                      │
│  ├─ 脏页列表: [P5, P10, P15]                                │
│  └─ 下一个页号: 100                                          │
│                                                             │
│      ┌──────────────────────────────────────────────────┐   │
│      │  嵌套事务 (txnid = 10, 不分配新ID)                 │   │
│      │  ├─ mt_parent = 顶层事务                          │   │
│      │  ├─ mt_child = NULL                               │   │
│      │  ├─ 脏页列表: [P20, P25]                          │   │
│      │  └─ 下一个页号: 150                                │   │
│      └──────────────────────────────────────────────────┘   │
│                                                             │
│  提交嵌套事务时：                                            │
│  • 脏页 [P20, P25] 合并到父事务                              │
│  • 释放的页面合并到父事务                                    │
│  • 父事务的下一个页号更新为 150                               │
└─────────────────────────────────────────────────────────────┘
```

### 嵌套事务限制

```c
#define MDB_TXN_HAS_CHILD  0x10

// 当子事务活跃时，父事务被阻塞
if (parent->mt_flags & MDB_TXN_HAS_CHILD) {
    return MDB_TXN_FULL;  // 父事务已有子事务
}

// 设置标志
parent->mt_flags |= MDB_TXN_HAS_CHILD;
txn->mt_parent = parent;
```

---

## 6.8 事务状态转换

```
┌─────────┐
│  创建   │
└────┬────┘
     │
     ▼
┌─────────┐    mdb_txn_commit()    ┌─────────┐
│  活跃   │ ──────────────────────>│  已提交 │
└────┬────┘                         └────┬────┘
     │                                  │
     │ mdb_txn_abort()                  │
     ▼                                  ▼
┌─────────┐                      ┌─────────┐
│  已中止 │                      │  已结束 │
└────┬────┘                      └─────────┘
     │
     ▼
┌─────────┐
│  销毁   │
└─────────┘

状态标志：
• 活跃:   mt_flags = 0
• 已提交: mt_flags |= MDB_TXN_FINISHED
• 已中止: mt_flags |= MDB_TXN_FINISHED
• 出错:   mt_flags |= MDB_TXN_ERROR
```

---

## 6.9 完整示例：事务生命周期跟踪器

### txn_tracker.c - 事务监控工具

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lmdb.h>
#include <pthread.h>
#include <unistd.h>

// 事务统计信息
typedef struct {
    pthread_t tid;
    int is_write;
    txnid_t txnid;
    time_t start_time;
    size_t num_ops;
} TxnInfo;

static TxnInfo active_txns[100];
static int txn_count = 0;
static pthread_mutex_t track_mutex = PTHREAD_MUTEX_INITIALIZER;

// 注册事务
void register_txn(pthread_t tid, int is_write, txnid_t txnid) {
    pthread_mutex_lock(&track_mutex);

    if (txn_count < 100) {
        active_txns[txn_count].tid = tid;
        active_txns[txn_count].is_write = is_write;
        active_txns[txn_count].txnid = txnid;
        active_txns[txn_count].start_time = time(NULL);
        active_txns[txn_count].num_ops = 0;
        txn_count++;
    }

    pthread_mutex_unlock(&track_mutex);
}

// 打印活跃事务
void print_active_txns() {
    pthread_mutex_lock(&track_mutex);

    printf("\n========== 活跃事务 (%d) ==========\n", txn_count);
    for (int i = 0; i < txn_count; i++) {
        char *type = active_txns[i].is_write ? "写" : "读";
        time_t elapsed = time(NULL) - active_txns[i].start_time;
        printf("[%2d] %s事务 | txnid=%llu | 线程=%lu | 耗时=%lds | 操作=%zu\n",
               i, type, (unsigned long long)active_txns[i].txnid,
               (unsigned long)active_txns[i].tid, elapsed,
               active_txns[i].num_ops);
    }
    printf("====================================\n\n");

    pthread_mutex_unlock(&track_mutex);
}

// 读事务工作线程
void *read_worker(void *arg) {
    MDB_env *env = (MDB_env *)arg;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;
    int rc;

    while (1) {
        rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
        if (rc != 0) {
            usleep(1000);
            continue;
        }

        register_txn(pthread_self(), 0, txn->mt_txnid);

        mdb_dbi_open(txn, NULL, 0, &dbi);

        // 读取一些数据
        key.mv_data = "test";
        key.mv_size = 4;
        mdb_get(txn, dbi, &key, &data);

        usleep(100000);  // 100ms

        mdb_txn_abort(txn);

        pthread_mutex_lock(&track_mutex);
        txn_count--;
        memmove(&active_txns[0], &active_txns[1],
                txn_count * sizeof(TxnInfo));
        pthread_mutex_unlock(&track_mutex);

        usleep(100000);  // 100ms
    }

    return NULL;
}

// 写事务工作线程
void *write_worker(void *arg) {
    MDB_env *env = (MDB_env *)arg;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;
    int rc;
    int counter = 0;

    while (1) {
        rc = mdb_txn_begin(env, NULL, 0, &txn);
        if (rc != 0) {
            if (rc == MDB_TXN_FULL) {
                printf("写事务被阻塞（已有活跃写事务）\n");
            }
            usleep(1000);
            continue;
        }

        register_txn(pthread_self(), 1, txn->mt_txnid);

        mdb_dbi_open(txn, NULL, 0, &dbi);

        // 写入一些数据
        char buf[64];
        snprintf(buf, sizeof(buf), "value-%d", counter++);
        key.mv_data = "test";
        key.mv_size = 4;
        data.mv_data = buf;
        data.mv_size = strlen(buf);

        mdb_put(txn, dbi, &key, &data, 0);

        usleep(50000);  // 50ms

        mdb_txn_commit(txn);

        pthread_mutex_lock(&track_mutex);
        txn_count--;
        memmove(&active_txns[0], &active_txns[1],
                txn_count * sizeof(TxnInfo));
        pthread_mutex_unlock(&track_mutex);

        usleep(200000);  // 200ms
    }

    return NULL;
}

int main() {
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;
    pthread_t readers[5], writers[2];
    int rc;

    // 创建环境
    rc = mdb_env_create(&env);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_create failed: %s\n", mdb_strerror(rc));
        return 1;
    }

    mdb_env_set_mapsize(env, 1024 * 1024 * 100);
    mdb_env_set_maxreaders(env, 20);

    rc = mdb_env_open(env, "./test_txn", 0, 0664);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_open failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    // 初始化数据库
    mdb_txn_begin(env, NULL, 0, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);
    MDB_val key = {"test", 4}, data = {"initial", 7};
    mdb_put(txn, dbi, &key, &data, 0);
    mdb_txn_commit(txn);

    // 启动读线程
    for (int i = 0; i < 5; i++) {
        pthread_create(&readers[i], NULL, read_worker, env);
    }

    // 启动写线程
    for (int i = 0; i < 2; i++) {
        pthread_create(&writers[i], NULL, write_worker, env);
    }

    // 监控循环
    for (int i = 0; i < 50; i++) {
        print_active_txns();
        sleep(1);
    }

    // 清理
    for (int i = 0; i < 5; i++) {
        pthread_cancel(readers[i]);
        pthread_join(readers[i], NULL);
    }
    for (int i = 0; i < 2; i++) {
        pthread_cancel(writers[i]);
        pthread_join(writers[i], NULL);
    }

    mdb_dbi_close(env, dbi);
    mdb_env_close(env);

    return 0;
}
```

---

## 6.10 完整示例：嵌套事务演示

### nested_txn_demo.c

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lmdb.h>

// 打印数据库内容
void print_db_contents(MDB_txn *txn, MDB_dbi dbi, const char *label) {
    MDB_cursor *cursor;
    MDB_val key, data;
    int rc;

    printf("\n========== %s ==========\n", label);

    mdb_cursor_open(txn, dbi, &cursor);

    rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    while (rc == 0) {
        printf("  %.*s -> %.*s\n",
               (int)key.mv_size, (char *)key.mv_data,
               (int)data.mv_size, (char *)data.mv_data);
        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    printf("================================\n\n");
}

// 演示嵌套事务的独立性
void demo_nested_independence() {
    MDB_env *env;
    MDB_txn *parent, *child;
    MDB_dbi dbi;
    MDB_val key, data;
    int rc;

    printf("\n\n=== 演示1: 嵌套事务独立性 ===\n");

    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024 * 100);
    mdb_env_open(env, "./test_nested1", 0, 0664);

    // 父事务
    mdb_txn_begin(env, NULL, 0, &parent);
    mdb_dbi_open(parent, NULL, 0, &dbi);

    // 父事务插入数据
    key.mv_data = "key1";
    key.mv_size = 4;
    data.mv_data = "parent_value1";
    data.mv_size = 12;
    mdb_put(parent, dbi, &key, &data, 0);

    print_db_contents(parent, dbi, "父事务提交前");

    // 子事务
    mdb_txn_begin(env, parent, 0, &child);

    // 子事务插入数据
    key.mv_data = "key2";
    key.mv_size = 4;
    data.mv_data = "child_value";
    data.mv_size = 11;
    mdb_put(child, dbi, &key, &data, 0);

    print_db_contents(child, dbi, "子事务中（包含父事务数据）");

    // 提交子事务
    mdb_txn_commit(child);
    print_db_contents(parent, dbi, "子事务提交后，父事务中");

    // 提交父事务
    mdb_txn_commit(parent);

    // 验证最终结果
    mdb_txn_begin(env, NULL, MDB_RDONLY, &parent);
    mdb_dbi_open(parent, NULL, 0, &dbi);
    print_db_contents(parent, dbi, "最终数据库内容");
    mdb_txn_abort(parent);

    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}

// 演示子事务回滚不影响父事务
void demo_child_rollback() {
    MDB_env *env;
    MDB_txn *parent, *child;
    MDB_dbi dbi;
    MDB_val key, data;
    int rc;

    printf("\n\n=== 演示2: 子事务回滚不影响父事务 ===\n");

    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024 * 100);
    mdb_env_open(env, "./test_nested2", 0, 0664);

    // 父事务
    mdb_txn_begin(env, NULL, 0, &parent);
    mdb_dbi_open(parent, NULL, 0, &dbi);

    key.mv_data = "parent_key";
    key.mv_size = 9;
    data.mv_data = "parent_data";
    data.mv_size = 11;
    mdb_put(parent, dbi, &key, &data, 0);

    printf("父事务插入: parent_key -> parent_data\n");

    // 子事务
    mdb_txn_begin(env, parent, 0, &child);

    key.mv_data = "child_key";
    key.mv_size = 9;
    data.mv_data = "child_data";
    data.mv_size = 10;
    mdb_put(child, dbi, &key, &data, 0);

    printf("子事务插入: child_key -> child_data\n");

    // 回滚子事务
    printf("回滚子事务...\n");
    mdb_txn_abort(child);

    print_db_contents(parent, dbi, "子事务回滚后，父事务中");

    // 父事务继续并提交
    key.mv_data = "another_key";
    key.mv_size = 10;
    data.mv_data = "another_data";
    data.mv_size = 12;
    mdb_put(parent, dbi, &key, &data, 0);

    mdb_txn_commit(parent);

    // 验证
    mdb_txn_begin(env, NULL, MDB_RDONLY, &parent);
    mdb_dbi_open(parent, NULL, 0, &dbi);
    print_db_contents(parent, dbi, "最终内容（无child_key）");
    mdb_txn_abort(parent);

    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}

// 演示父事务无法在子事务活跃时操作
void demo_parent_blocked() {
    MDB_env *env;
    MDB_txn *parent, *child;
    MDB_dbi dbi;
    MDB_val key, data;
    int rc;

    printf("\n\n=== 演示3: 子事务活跃时父事务被阻塞 ===\n");

    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024 * 100);
    mdb_env_open(env, "./test_nested3", 0, 0664);

    mdb_txn_begin(env, NULL, 0, &parent);
    mdb_dbi_open(parent, NULL, 0, &dbi);

    mdb_txn_begin(env, parent, 0, &child);

    // 尝试在父事务上操作（应该失败或被阻塞）
    key.mv_data = "test";
    key.mv_size = 4;
    data.mv_data = "data";
    data.mv_size = 4;

    rc = mdb_put(parent, dbi, &key, &data, 0);
    if (rc == MDB_TXN_FULL) {
        printf("正确：父事务在子事务活跃时无法操作 (MDB_TXN_FULL)\n");
    } else {
        printf("警告：父事务操作返回 %d\n", rc);
    }

    mdb_txn_abort(child);
    mdb_txn_abort(parent);

    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}

int main() {
    demo_nested_independence();
    demo_child_rollback();
    demo_parent_blocked();

    printf("\n所有嵌套事务演示完成！\n");

    return 0;
}
```

---

## 6.11 常见问题解答

### Q1: 为什么读事务不需要锁就能读取数据？

**A:** 这是 LMDB MVCC 架构的核心优势：

1. **一致性快照**：读事务在开始时记录一个事务ID，之后只读取该事务ID对应版本的页面
2. **写时复制**：写操作总是复制页面到新位置，原页面保持不变
3. **无需阻塞**：读事务读取的是"旧"页面，与写操作互不干扰

```
时间线：
T1: 读事务开始 (txnid=10)
T2: 写事务修改页面 P5 -> 复制到 P100
T3: 读事务仍然读取 P5（旧版本）
T4: 写事务提交 (txnid=11)
T5: 读事务继续读取 P5（依然一致）
```

### Q2: 写事务为什么同时只能有一个？

**A:** LMDB 采用单写入者模型的原因：

1. **简化实现**：避免写操作的锁竞争和死锁
2. **性能优势**：批量写入，减少磁盘同步次数
3. **MVCC兼容**：多个写事务需要复杂的版本管理

虽然只有一个写事务可以活跃，但：
- 多个写事务可以排队等待
- 读事务完全不受影响
- 写操作非常快（毫秒级）

### Q3: 嵌套事务的脏页如何合并到父事务？

**A:** 嵌套事务提交时的合并过程：

```c
// 伪代码
void merge_child_to_parent(MDB_txn *child) {
    MDB_txn *parent = child->mt_parent;

    // 1. 合并脏页列表
    for each dirty_page in child->mt_u.dirty_list {
        add to parent->mt_u.dirty_list;
    }

    // 2. 合并释放的页面
    merge_idl(parent->mt_free_pgs, child->mt_free_pgs);

    // 3. 更新下一个页号
    parent->mt_next_pgno = child->mt_next_pgno;

    // 4. 清除父事务的阻塞标志
    parent->mt_flags &= ~MDB_TXN_HAS_CHILD;
}
```

### Q4: 读者表满了怎么办？

**A:** 错误码 `MDB_READERS_FULL` 的处理：

```c
// 解决方案：

// 1. 增加最大读者数
mdb_env_set_maxreaders(env, 256);

// 2. 检查是否有泄漏的读事务
// 确保每个读事务都正确中止或提交

// 3. 使用连接池复用读事务
// 而不是为每个操作创建新事务
```

### Q5: 事务ID会溢出吗？

**A:** `txnid_t` 是 64 位无符号整数：

- 最大值：2^64 - 1 = 18,446,744,073,709,551,615
- 每秒1000次写事务：需要 5840 万年才会溢出
- 实际上不用担心溢出问题

---

## 6.12 今日练习

1. **编译运行示例程序**：
   - `txn_tracker.c` - 观察并发事务
   - `nested_txn_demo.c` - 学习嵌套事务

2. **调试练习**：
   ```bash
   gdb ./mtest
   (gdb) break mdb_txn_begin
   (gdb) run
   (gdb) print *txn
   (gdb) print txn->mt_txnid
   (gdb) print txn->mt_flags
   ```

3. **观察读者表**：
   - 运行多个读事务
   - 使用 mdb_stat 观察状态

---

## 6.13 思考题

1. 为什么读事务不需要锁就能读取数据？
2. 写事务为什么同时只能有一个？
3. 嵌套事务的脏页如何合并到父事务？
4. 读者表满了怎么办？
5. 事务ID在崩溃恢复中的作用是什么？

---

## 明天预告

Day 7 将继续讲解事务管理（下）。我们将学习：
- 事务提交的完整流程
- 事务中止的处理
- 脏页的刷新
- 元数据页的更新

事务提交是保证持久性的关键！

---
**参考文献：**
- mdb.c: 1309-1409 (MDB_txn 结构)
- mdb.c: 3061-3180 (mdb_txn_renew0)
- mdb.c: 3218-3250 (mdb_txn_begin)
- mdb.c: 830-850 (MDB_reader)
