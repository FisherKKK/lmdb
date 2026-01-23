# LMDB 底层实现 14天课程 - Day 3

## 数据库环境 (MDB_env) - LMDB 的容器

欢迎回来！今天我们将深入研究 LMDB 的核心容器 - `MDB_env`。环境是 LMDB 的顶层抽象，理解它的工作原理是掌握整个系统的关键。

---

## 今天的目标

1. 理解 MDB_env 的结构
2. 掌握环境的创建和初始化流程
3. 了解锁文件的结构和作用
4. 理解读者/写入者协调机制

---

## 3.1 MDB_env 结构体详解

`MDB_env` 是整个数据库环境的代表，包含了所有必要的信息（mdb.c:1516）：

```c
struct MDB_env {
    // === 文件句柄 ===
    HANDLE  me_fd;      // 主数据文件 (data.mdb)
    HANDLE  me_lfd;     // 锁文件 (lock.mdb)
    HANDLE  me_mfd;     // 用于写入元数据页的文件句柄

    // === 标志位 ===
    uint32_t me_flags;  // 环境标志
#define MDB_FATAL_ERROR  0x80000000U  // 致命错误
#define MDB_ENV_ACTIVE   0x20000000U  // 环境已激活
#define MDB_ENV_TXKEY    0x10000000U  // me_txkey 已设置
#define MDB_FSYNCONLY    0x08000000U  // fdatasync 不可靠

    // === 页面配置 ===
    unsigned int me_psize;     // 数据库页面大小
    unsigned int me_os_psize;  // 操作系统页面大小

    // === 读者配置 ===
    unsigned int me_maxreaders;  // 读者表大小
    volatile int me_close_readers; // 关闭时的读者计数

    // === 数据库管理 ===
    MDB_dbi   me_numdbs;   // 已打开的数据库数量
    MDB_dbi   me_maxdbs;   // 数据库表大小

    // === 进程信息 ===
    MDB_PID_T me_pid;      // 当前进程 ID

    // === 路径和映射 ===
    char      *me_path;    // 数据库文件路径
    char      *me_map;     // 数据文件的内存映射

    // === 事务信息 ===
    MDB_txninfo *me_txns;  // 锁文件的内存映射
    MDB_meta    *me_metas[NUM_METAS]; // 两个元数据页指针
    MDB_txn     *me_txn;   // 当前写事务
    MDB_txn     *me_txn0;  // 预分配的写事务

    // === 映射配置 ===
    mdb_size_t  me_mapsize;  // 内存映射大小
    MDB_OFF_T   me_size;     // 当前文件大小
    pgno_t      me_maxpg;    // 最大页号 (me_mapsize / me_psize)

    // === 数据库元信息 ===
    MDB_dbx     *me_dbxs;     // 静态数据库信息数组
    uint16_t    *me_dbflags;  // 数据库标志数组
    unsigned int *me_dbiseqs; // 数据库序列号数组

    // === 线程本地存储 ===
    pthread_key_t me_txkey;   // 读者线程的线程本地键

    // === 页面管理 ===
    txnid_t      me_pgoldest; // 最老读者的事务ID
    MDB_pgstate  me_pgstate;  // 来自 freeDB 的旧页面状态
    MDB_page     *me_dpages;  // 可重用的 malloc 块列表

    // === 空闲列表 ===
    MDB_IDL  me_free_pgs;     // 写事务中释放的页面
    MDB_ID2L me_dirty_list;   // 写事务中修改的页面

    // === 限制 ===
    int    me_maxfree_1pg;    // 单个溢出页可容纳的最大空闲列表项
    unsigned int me_nodemax;  // 页面上节点的最大大小
#if !(MDB_MAXKEYSIZE)
    unsigned int me_maxkey;   // 键的最大大小
#endif

    // === 锁 ===
    mdb_mutex_t me_rmutex;    // 读者锁
    mdb_mutex_t me_wmutex;    // 写入者锁

#ifdef MDB_VL32
    MDB_ID3L       me_rpages;     // 全局映射页面块列表
    pthread_mutex_t me_rpmutex;   // 访问 me_rpages 的互斥锁
#endif

    // === 用户上下文 ===
    void            *me_userctx;      // 用户设置的上下午
    MDB_assert_func *me_assert_func; // 断言失败回调
};
```

---

## 3.2 环境创建流程

### mdb_env_create() - 创建环境句柄

```c
int mdb_env_create(MDB_env **env)
{
    MDB_env *e;

    // 1. 分配环境结构
    e = calloc(1, sizeof(MDB_env));
    if (!e)
        return ENOMEM;

    // 2. 设置默认值
    e->me_maxreaders = DEFAULT_READERS;  // 默认 126
    e->me_maxdbs = e->me_numdbs = CORE_DBS;  // 默认 2 (free + main)
    e->me_fd = INVALID_HANDLE_VALUE;
    e->me_lfd = INVALID_HANDLE_VALUE;
    e->me_mfd = INVALID_HANDLE_VALUE;

#ifdef MDB_USE_POSIX_SEM
    e->me_rmutex = SEM_FAILED;
    e->me_wmutex = SEM_FAILED;
#endif

    // 3. 记录进程 ID 和获取页面大小
    e->me_pid = getpid();
    GET_PAGESIZE(e->me_os_psize);

    // 4. Valgrind 支持
    VGMEMP_CREATE(e, 0, 0);

    *env = e;
    return MDB_SUCCESS;
}
```

**要点**：
- 只分配内存，不打开任何文件
- 设置默认配置
- 获取操作系统页面大小
- 此时尚未映射内存

---

## 3.3 环境打开流程

### mdb_env_open() - 打开环境

打开环境的完整流程较为复杂，让我们分步讲解：

```
mdb_env_open()
    │
    ├─> 1. 参数验证和路径处理
    │
    ├─> 2. 打开/创建数据文件 (me_fd)
    │
    ├─> 3. 打开/创建锁文件 (me_lfd)
    │
    ├─> 4. 映射锁文件到内存 (me_txns)
    │       │
    │       └─> 初始化读者表和互斥锁
    │
    ├─> 5. 读取/初始化元数据
    │       │
    │       ├─> 如果是新数据库：初始化元数据页
    │       └─> 如果是现有数据库：读取并验证元数据
    │
    ├─> 6. 映射数据文件到内存 (mdb_env_map)
    │       │
    │       └─> 设置 me_metas[0] 和 me_metas[1]
    │
    ├─> 7. 初始化环境配置
    │       │
    │       ├─> 设置页面大小 (me_psize)
    │       ├─> 设置最大页号 (me_maxpg)
    │       └─> 分配数据库信息数组 (me_dbxs)
    │
    └─> 8. 创建预分配事务 (me_txn0)
```

### 关键代码片段

```c
// 打开锁文件并映射到内存
{
    int rc = open(lfd_path, O_RDWR|O_CREAT, 0664);
    if (rc < 0) return errno;
    env->me_lfd = rc;

    // 设置锁文件大小
    rsize = sizeof(MDB_txninfo) + env->me_maxreaders * sizeof(MDB_reader);
    if (ftruncate(env->me_lfd, rsize) < 0)
        return ErrCode();

    // 映射锁文件
    void *m = mmap(NULL, rsize, PROT_READ|PROT_WRITE,
                   MAP_SHARED, env->me_lfd, 0);
    if (m == MAP_FAILED)
        return ErrCode();
    env->me_txns = m;
}
```

---

## 3.4 锁文件的结构

### MDB_txninfo - 锁文件内容

```c
typedef struct MDB_txninfo {
    // === 互斥锁 ===
    mdb_mutex_t mti_rmutex;  // 读者锁
    mdb_mutex_t mti_wmutex;  // 写入者锁

#ifdef _WIN32
    // Windows 特定字段
    uint32_t mti_numreaders; // 读者数量
    mdb_mutex_t mti_rm_action; // 读者管理锁
#else
    // Unix 特定字段
    unsigned volatile int mti_numreaders; // 读者数量
    char pad1[(sizeof(pthread_mutex_t) * 2) - sizeof(unsigned)];
#endif

    // === 读者表 ===
    // 动态大小数组，实际大小 = me_maxreaders
    MDB_reader mti_readers[1];
} MDB_txninfo;
```

### MDB_reader - 读者记录

```c
typedef struct MDB_rxbody {
    // 该读者正在使用的事务ID
    txnid_t         mr_txnid;
    // 该读者的进程ID
    MDB_PID_T       mr_pid;
    // 该读者的线程ID
    MDB_THR_T       mr_tid;
} MDB_rxbody;

typedef struct MDB_reader {
    // 原子性地访问 mr_txnid
    union {
        MDB_rxbody mrx;
        __attribute__((aligned(sizeof(pthread_mutex_t)))) uint64_t mb_pad;
    } mru;
    // 该读者是否是活跃的
    volatile unsigned mr_flags;
#define MR_DEAD     1  // 读者已死（进程终止）
#define MR_EOF      2  // 读取到数据末尾
} MDB_reader;
```

### 锁文件布局

```
┌─────────────────────────────────────────────────────────────┐
│                      MDB_txninfo                            │
├─────────────────────────────────────────────────────────────┤
│  mti_rmutex          │ 读者互斥锁                            │
├─────────────────────────────────────────────────────────────┤
│  mti_wmutex          │ 写入者互斥锁                          │
├─────────────────────────────────────────────────────────────┤
│  mti_numreaders      │ 当前读者数量                          │
├─────────────────────────────────────────────────────────────┤
│  mti_readers[0]      │ 读者槽位 0                           │
│  ├─ mr_txnid         │   事务ID                             │
│  ├─ mr_pid           │   进程ID                             │
│  ├─ mr_tid           │   线程ID                             │
│  └─ mr_flags         │   标志                               │
├─────────────────────────────────────────────────────────────┤
│  mti_readers[1]      │ 读者槽位 1                           │
├─────────────────────────────────────────────────────────────┤
│  ...                                                         │
├─────────────────────────────────────────────────────────────┤
│  mti_readers[n]      │ 读者槽位 n (n = me_maxreaders - 1)   │
└─────────────────────────────────────────────────────────────┘
```

---

## 3.5 读者/写入者协调机制

### 读事务的开始

```c
int mdb_txn_begin(MDB_env *env, MDB_txn *parent, unsigned int flags, MDB_txn **ret)
{
    if (!(flags & MDB_RDONLY)) {
        // 写事务：需要获取写入者锁
        LOCK_MUTEX_W(env);
    } else {
        // 读事务：在读者表中注册
        txnid_t txnid = env->me_me_metas[me_numdbs].mm_txnid;

        // 查找空闲槽位
        for (i = 0; i < env->me_maxreaders; i++) {
            reader = &env->me_txns->mti_readers[i];
            if (reader->mr_pid == 0) {
                // 找到空闲槽位
                reader->mr_pid = getpid();
                reader->mr_tid = pthread_self();
                reader->mr_txnid = txnid;
                break;
            }
        }
    }
    // ...
}
```

### 写事务的开始

```c
// 写事务流程：
// 1. 获取写入者锁 (LOCK_MUTEX_W)
// 2. 检查是否有活跃的读事务
// 3. 分配新的事务ID
// 4. 准备修改页面

// 锁宏定义
#define LOCK_MUTEX_W(env)  LOCK_MUTEX0((env)->me_wmutex)
#define UNLOCK_MUTEX_W(env) UNLOCK_MUTEX((env)->me_wmutex)
```

---

## 3.6 环境标志选项

### MDB 环境标志

| 标志 | 值 | 说明 |
|------|-----|------|
| MDB_FIXEDMAP | 0x01 | 使用固定地址映射 |
| MDB_NOSUBDIR | 0x02 | 数据库文件不使用子目录 |
| MDB_NOSYNC | 0x10000 | 不同步到磁盘 |
| MDB_RDONLY | 0x20000 | 只读模式 |
| MDB_NOMETASYNC | 0x40000 | 不同步元数据页 |
| MDB_WRITEMAP | 0x80000 | 使用写映射 |
| MDB_MAPASYNC | 0x100000 | 异步刷新 |
| MDB_NOTLS | 0x200000 | 读事务不使用线程本地存储 |
| MDB_NOLOCK | 0x400000 | 不使用锁文件 |
| MDB_NORDAHEAD | 0x800000 | 关闭预读 |
| MDB_NOMEMINIT | 0x1000000 | 不初始化内存映射 |

### 常用组合

```c
// 高性能写入（可接受数据丢失风险）
mdb_env_open(env, path, MDB_WRITEMAP | MDB_NOSYNC | MDB_MAPASYNC, 0664);

// 默认安全模式
mdb_env_open(env, path, 0, 0664);

// 只读模式
mdb_env_open(env, path, MDB_RDONLY, 0664);
```

---

## 3.7 环境关闭流程

### mdb_env_close() - 关闭环境

```c
void mdb_env_close(MDB_env *env)
{
    // 1. 等待所有读者退出
    while (env->me_close_readers > 0) {
        // 等待...
    }

    // 2. 取消映射数据文件
    if (env->me_map) {
        munmap(env->me_map, env->me_mapsize);
    }

    // 3. 取消映射锁文件
    if (env->me_txns) {
        munmap(env->me_txns, sizeof(MDB_txninfo) +
               env->me_maxreaders * sizeof(MDB_reader));
    }

    // 4. 关闭文件句柄
    if (env->me_fd != INVALID_HANDLE_VALUE)
        close(env->me_fd);
    if (env->me_lfd != INVALID_HANDLE_VALUE)
        close(env->me_lfd);
    if (env->me_mfd != INVALID_HANDLE_VALUE)
        close(env->me_mfd);

    // 5. 释放其他资源
    free(env->me_dbxs);
    // ...

    // 6. 释放环境结构
    free(env);
}
```

---

## 3.8 读者检查和清理

### mdb_reader_check() - 检查过期读者

```c
int mdb_env_copyfd2(MDB_env *env, HANDLE fd, unsigned int flags)
{
    // 检查是否有死锁的读者
    int dead = 0;
    for (i = 0; i < env->me_maxreaders; i++) {
        reader = &env->me_txns->mti_readers[i];
        if (reader->mr_pid && reader->mr_txnid) {
            // 检查进程是否还存在
            if (!reader_alive(reader)) {
                reader->mr_pid = 0;
                reader->mr_txnid = 0;
                dead++;
            }
        }
    }
    return dead;
}
```

---

## 3.9 环境配置函数

### 设置配置项

```c
// 设置映射大小
int mdb_env_set_mapsize(MDB_env *env, mdb_size_t size);

// 设置最大数据库数
int mdb_env_set_maxdbs(MDB_env *env, MDB_dbi dbs);

// 设置最大读者数
int mdb_env_set_maxreaders(MDB_env *env, unsigned int readers);
```

### 使用示例

```c
MDB_env *env;
mdb_env_create(&env);

// 配置环境
mdb_env_set_mapsize(env, 1024 * 1024 * 1024);  // 1GB
mdb_env_set_maxdbs(env, 16);                   // 最多 16 个数据库
mdb_env_set_maxreaders(env, 256);              // 最多 256 个并发读者

// 打开环境
mdb_env_open(env, "./mydb", 0, 0664);
```

---

## 3.10 完整示例：环境管理实践

### 示例：动态调整环境配置

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lmdb.h>

void print_env_stats(MDB_env *env) {
    MDB_stat stat;

    printf("=== Environment Statistics ===\n");
    printf("Page size: %u bytes\n", env->me_psize);
    printf("Map size: %.2f MB\n", (double)env->me_mapsize / (1024 * 1024));
    printf("Max pages: %u\n", env->me_maxpg);
    printf("Max readers: %u\n", env->me_maxreaders);
    printf("Max DBs: %u\n", env->me_maxdbs);

    // 获取数据库统计
    MDB_txn *txn;
    if (mdb_txn_begin(env, NULL, MDB_RDONLY, &txn) == 0) {
        if (mdb_stat(txn, 1, &stat) == 0) {
            printf("\nMain DB Stats:\n");
            printf("  Branch pages: %u\n", stat.ms_branch_pages);
            printf("  Leaf pages: %u\n", stat.ms_leaf_pages);
            printf("  Overflow pages: %u\n", stat.ms_overflow_pages);
            printf("  Entries: %zu\n", stat.ms_entries);
        }
        mdb_txn_abort(txn);
    }
}

void print_readers(MDB_env *env) {
    if (!env->me_txns) {
        printf("No lock file (MDB_NOLOCK mode?)\n");
        return;
    }

    MDB_txninfo *ti = env->me_txns;
    unsigned int num = ti->mti_numreaders;

    printf("\n=== Reader Table (%u readers) ===\n", num);
    for (unsigned int i = 0; i < num; i++) {
        MDB_reader *r = &ti->mti_readers[i];
        if (r->mr_pid) {
            printf("Reader %2u: pid=%u, txnid=%llu, tid=%lu\n",
                   i, r->mr_pid, (unsigned long long)r->mr_txnid,
                   (unsigned long)r->mr_tid);
        }
    }
}

int main() {
    MDB_env *env;
    int rc;

    printf("=== LMDB Environment Management Demo ===\n\n");

    // 1. 创建环境
    printf("Step 1: Creating environment...\n");
    rc = mdb_env_create(&env);
    if (rc) {
        fprintf(stderr, "mdb_env_create failed: %s\n", mdb_strerror(rc));
        return 1;
    }

    // 2. 配置环境
    printf("Step 2: Configuring environment...\n");
    mdb_env_set_mapsize(env, 1024 * 1024 * 50);  // 50MB
    mdb_env_set_maxdbs(env, 8);
    mdb_env_set_maxreaders(env, 64);

    // 3. 打开环境
    printf("Step 3: Opening environment...\n");
    rc = mdb_env_open(env, "./testdb", MDB_FIXEDMAP | MDB_NOSYNC, 0664);
    if (rc) {
        fprintf(stderr, "mdb_env_open failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    // 4. 打印环境信息
    print_env_stats(env);

    // 5. 测试多个数据库
    printf("\n=== Testing Multiple Databases ===\n");
    MDB_txn *txn;
    mdb_txn_begin(env, NULL, 0, &txn);

    // 打开几个命名数据库
    MDB_dbi users_db, posts_db, comments_db;
    mdb_dbi_open(txn, "users", MDB_CREATE, &users_db);
    mdb_dbi_open(txn, "posts", MDB_CREATE, &posts_db);
    mdb_dbi_open(txn, "comments", MDB_CREATE, &comments_db);

    printf("Opened databases:\n");
    printf("  users: dbi=%d\n", users_db);
    printf("  posts: dbi=%d\n", posts_db);
    printf("  comments: dbi=%d\n", comments_db);

    mdb_txn_commit(txn);

    // 6. 查看读者表
    print_readers(env);

    // 7. 清理过期读者（如果有）
    int dead;
    rc = mdb_reader_check(env, &dead);
    if (rc > 0) {
        printf("\nFound %d stale reader(s)\n", dead);
    }

    // 8. 关闭环境
    printf("\n=== Closing environment ===\n");
    mdb_env_close(env);

    printf("Done!\n");
    return 0;
}
```

编译运行：
```bash
gcc -o env_demo env_demo.c -llmdb
./env_demo
```

---

## 3.11 实践：观察环境结构

### 练习 1: 运行环境管理示例
编译并运行上面的 `env_demo.c`，观察：
- 环境配置信息
- 数据库句柄分配
- 读者表状态

### 练习 2: 查看锁文件结构

```bash
# 创建测试环境
mkdir -p testdb && cd testdb
mdb_load -f input.txt  # 假设有输入文件

# 查看锁文件大小
ls -lh lock.mdb

# 使用 hexdump 查看内容
hexdump -C lock.mdb | head -50

# 使用 od 查看另一种格式
od -t x4 -N 512 lock.mdb
```

### 练习 3: 监控读者表变化

```c
// 创建一个监控程序
#include <stdio.h>
#include <unistd.h>
#include <lmdb.h>

void monitor_readers(MDB_env *env) {
    MDB_txninfo *ti;
    unsigned int last_count = 0;

    while (1) {
        if (!env->me_txns) {
            printf("No lock file\n");
            break;
        }

        ti = env->me_txns;
        unsigned int num = ti->mti_numreaders;

        if (num != last_count) {
            printf("[%ld] Active readers: %u\n",
                   (long)time(NULL), num);

            for (unsigned int i = 0; i < num; i++) {
                MDB_reader *r = &ti->mti_readers[i];
                if (r->mr_pid) {
                    printf("  [%u] pid=%u, txnid=%llu\n",
                           i, r->mr_pid,
                           (unsigned long long)r->mr_txnid);
                }
            }
            last_count = num;
        }

        sleep(1);
    }
}
```

---

## 3.12 今日练习

1. **代码阅读**：在 mdb.c 中找到并阅读以下函数：
   - `mdb_env_create()` - 行 4498
   - `mdb_env_open()` - 约 4900 行
   - `mdb_env_close()` - 约 5200 行

2. **实验**：创建一个测试程序，尝试以下配置：
   - 不同的映射大小（1MB, 100MB, 1GB）
   - 不同的最大读者数（1, 10, 100, 1000）
   - 使用 MDB_WRITEMAP 标志

3. **调试**：使用 GDB 设置断点在 `mdb_env_open`，单步跟踪执行流程

---

## 3.13 常见问题解答

### Q1: 什么时候需要增加最大读者数？

**A:** 出现 `MDB_READERS_FULL` 错误时。常见场景：
- 高并发服务器应用
- 每个请求创建读事务（短生命周期）
- 连接池或线程池配置

```c
// 检测并处理
rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
if (rc == MDB_READERS_FULL) {
    // 需要重启并增加 maxreaders
    fprintf(stderr, "Too many concurrent readers!\n");
}
```

### Q2: 多个数据库有什么用途？

**A:** 用于数据分区和索引：
- `users` - 用户数据
- `posts` - 帖文数据
- `index_username` - 用户名索引
- `index_email` - 邮箱索引

每个独立的数据库有自己的 B+ 树和配置。

### Q3: MDB_FIXEDMAP 的作用是什么？

**A:** 使用固定的映射地址：
- 加速映射（避免地址搜索）
- 需要知道可用地址
- 主要用于调试和特殊场景

### Q4: 为什么需要清理过期读者？

**A:** 过期读者会浪费资源：
- 占用读者表槽位
- 阻止页面回收
- 可能导致数据库文件增长

使用 `mdb_reader_check()` 定期清理。

---

## 3.14 实用工具函数

### 环境状态检查

```c
int check_env_health(MDB_env *env) {
    int rc, dead;

    // 检查过期读者
    rc = mdb_reader_check(env, &dead);
    if (rc < 0) {
        fprintf(stderr, "mdb_reader_check failed: %s\n",
                mdb_strerror(rc));
        return -1;
    }

    if (dead > 0) {
        printf("Warning: Found %d stale readers\n", dead);
    }

    // 检查映射空间使用
    if (env->me_mapsize > 0) {
        size_t used = env->me_next_pgno * env->me_psize;
        size_t total = env->me_mapsize;
        size_t percent = (used * 100) / total;

        printf("Map space usage: %zu / %zu (%zu%%)\n",
               used, total, percent);

        if (percent > 90) {
            printf("Warning: Map space nearly full!\n");
        }
    }

    return 0;
}
```

### 获取环境详细信息

```c
void dump_env_info(MDB_env *env) {
    printf("\n=== LMDB Environment Dump ===\n");

    // 基本信息
    printf("Path: %s\n", env->me_path ? env->me_path : "(none)");
    printf("PID: %u\n", env->me_pid);

    // 映射信息
    printf("Map address: %p\n", env->me_map);
    printf("Map size: %zu bytes\n", env->me_mapsize);
    printf("File size: %lld bytes\n", (long long)env->me_size);

    // 页面信息
    printf("Page size: %u bytes\n", env->me_psize);
    printf("Max pages: %u\n", env->me_maxpg);
    printf("Next page: %u\n", env->me_next_pgno);

    // 配置
    printf("Max readers: %u\n", env->me_maxreaders);
    printf("Max DBs: %u\n", env->me_maxdbs);
    printf("Num DBs: %u\n", env->me_numdbs);

    // 标志
    printf("Flags: 0x%x\n", env->me_flags);

    // 锁文件
    if (env->me_txns) {
        printf("Lock file mapped\n");
        printf("Num readers: %u\n", env->me_txns->mti_numreaders);
    } else {
        printf("No lock file (MDB_NOLOCK?)\n");
    }

    printf("====================================\n\n");
}
```

---

## 3.15 思考题

1. 为什么需要两个元数据页而不是一个？
2. 锁文件为什么要映射到内存而不是使用普通文件读写？
3. 读者表的大小如何影响并发性能？
4. MDB_WRITEMAP 标志如何影响性能和数据安全性？
5. 什么时候应该使用多个命名数据库？

---

## 明天预告

Day 4 将深入讲解页面结构。我们将学习：
- 页面的内部布局
- 页面标志和类型
- 节点在页面中的组织
- 空间管理策略

页面是 LMDB 存储的基本单位，理解它至关重要！

---
**参考文献：**
- mdb.c: 1516-1600 (MDB_env 结构定义)
- mdb.c: 4498-4525 (mdb_env_create)
- mdb.c: 830-900 (MDB_txninfo, MDB_reader)
- lmdb.h: mdb_env_create, mdb_env_open 文档
