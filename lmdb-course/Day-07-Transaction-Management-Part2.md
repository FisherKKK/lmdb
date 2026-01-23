# LMDB 底层实现 14天课程 - Day 7

## 事务管理（下）- 事务提交与中止

欢迎回来！今天我们将继续深入事务管理，重点讲解事务的**提交**和**中止**过程。这是理解 LMDB 如何保证持久性和原子性的关键。

---

## 今天的目标

1. 理解事务提交的完整流程
2. 掌握元数据页的更新机制
3. 理解事务中止的处理
4. 学习嵌套事务的提交

---

## 7.1 事务提交概览

### 提交流程图

```
_mdb_txn_commit()
    │
    ├─> 1. 检查事务状态
    │       ├─ 是否有子事务？
    │       ├─ 是否只读事务？
    │       └─ 是否已完成/出错？
    │
    ├─> 2. 如果是嵌套事务
    │       ├─ 合并脏页到父事务
    │       ├─ 合并空闲页列表
    │       ├─ 更新父事务的数据库表
    │       └─ 跳到步骤 7
    │
    ├─> 3. 刷新脏页到磁盘
    │       ├─ mdb_page_flush()
    │       ├─ 使用 writev() 批量写入
    │       └─ 清除 P_DIRTY 标志
    │
    ├─> 4. 更新元数据页
    │       ├─ 选择新的元数据页 (txnid & 1)
    │       ├─ 更新 mm_txnid
    │       ├─ 更新 mm_last_pg
    │       └─ 更新 mm_dbs[] (Free DB 和 Main DB)
    │
    ├─> 5. 同步到磁盘
    │       ├─ 根据 flags 决定同步策略
    │       ├─ MDB_NOSYNC: 跳过同步
    │       ├─ MDB_NOMETASYNC: 只同步数据
    │       └─ 默认: 同步元数据和数据
    │
    ├─> 6. 更新环境状态
    │       ├─ 增加 me_txns->mti_txnid
    │       ├─ 清除 env->me_txn
    │       └─ 释放写入者锁
    │
    └─> 7. 清理事务资源
        ├─ 关闭游标
        ├─ 释放内存
        └─ 设置 MDB_TXN_FINISHED 标志
```

---

## 7.2 嵌套事务提交

### 合并到父事务 (mdb.c:4007)

```c
if (txn->mt_parent) {
    MDB_txn *parent = txn->mt_parent;

    // 1. 合并空闲页列表
    rc = mdb_midl_append_list(&parent->mt_free_pgs, txn->mt_free_pgs);
    if (rc) goto fail;
    mdb_midl_free(txn->mt_free_pgs);

    // 2. 更新父事务的下一个页号
    parent->mt_next_pgno = txn->mt_next_pgno;

    // 3. 关闭并合并游标
    mdb_cursors_close(txn, 1);  // 1 = 合并到父事务

    // 4. 更新数据库表
    memcpy(parent->mt_dbs, txn->mt_dbs,
           txn->mt_numdbs * sizeof(MDB_db));
    parent->mt_numdbs = txn->mt_numdbs;

    // 5. 合并脏页列表
    //    （从父事务的溢出列表中移除子事务的脏页）
    //    （从子事务的溢出列表中移除父事务的脏页）

    // 6. 合并溢出页列表
    if (txn->mt_spill_pgs) {
        // 将溢出页重新标记为脏页
        for (i = 1; i <= txn->mt_spill_pgs[0]; i++) {
            pgno_t pgno = txn->mt_spill_pgs[i];
            // 添加到父事务的脏页列表
        }
    }
}
```

### 嵌套事务状态变化

```
提交前：
父事务:
  mt_next_pgno = 100
  mt_free_pgs = [5, 10]
  mt_dirty_list = [P1, P2]

子事务:
  mt_next_pgno = 150
  mt_free_pgs = [15, 20]
  mt_dirty_list = [P3, P4]

提交后：
父事务:
  mt_next_pgno = 150      ← 从子事务更新
  mt_free_pgs = [5, 10, 15, 20]  ← 合并
  mt_dirty_list = [P1, P2, P3, P4]  ← 合并

子事务:
  (已销毁)
```

---

## 7.3 刷新脏页到磁盘

### mdb_page_flush() (mdb.c:3920)

```c
static int mdb_page_flush(MDB_txn *txn, int keep)
{
    MDB_env *env = txn->mt_env;
    MDB_ID2L dp = txn->mt_u.dirty_list;
    unsigned int i, j, n;
    int rc = 0;
    size_t sz, pos = 0;
    ssize_t wsize;
    off_t wpos;

    // 构建写缓冲区
    // 使用 writev() 进行批量写入

    // 计算需要写入的页面数量
    n = dp[0].mid;

    // 批量写入页面
    for (i = 0; i < n; ) {
        // 准备 iovec 数组
        struct iovec iov[MDB_COMMIT_PAGES];
        size_t niov = 0;

        // 收集连续的页面
        for (j = i; j < n && niov < MDB_COMMIT_PAGES; j++) {
            MDB_page *mp = dp[j].mptr;
            pgno_t pgno = dp[j].mid;

            // 计算文件位置
            pos = pgno * env->me_psize;

            // 如果不连续，先写入当前批
            if (j > i && pgno != dp[j-1].mid + 1)
                break;

            iov[niov].iov_base = mp;
            iov[niov].iov_len = env->me_psize;
            niov++;
        }

        // 计算写入位置
        wpos = dp[i].mid * env->me_psize;

        // 使用 writev 批量写入
        wsize = pwritev(env->me_fd, iov, niov, wpos);

        // 检查写入结果
        if (wsize < 0) {
            rc = ErrCode();
            goto fail;
        }

        // 清除 P_DIRTY 标志
        for (k = i; k < j; k++) {
            MDB_page *mp = dp[k].mptr;
            mp->mp_flags &= ~P_DIRTY;
        }

        i = j;
    }

    return MDB_SUCCESS;
}
```

### writev() 批量写入

```
传统方式（多次 write）：
write(fd, page1, 4096);
write(fd, page2, 4096);
write(fd, page3, 4096);
// 3 次系统调用

LMDB 方式（一次 writev）：
struct iovec iov[3];
iov[0].iov_base = page1; iov[0].iov_len = 4096;
iov[1].iov_base = page2; iov[1].iov_len = 4096;
iov[2].iov_base = page3; iov[2].iov_len = 4096;
pwritev(fd, iov, 3, offset);
// 1 次系统调用
```

---

## 7.4 元数据页更新

### 更新元数据 (mdb.c:4140)

```c
// 选择要更新的元数据页
int meta_num = txn->mt_txnid & 1;
MDB_meta *meta = env->me_metas[meta_num];
MDB_meta *m2 = &((MDB_metabuf *)meta)->mb_metabuf.mm_meta;

// 1. 更新事务ID
m2->mm_txnid = txn->mt_txnid;

// 2. 更新最后页号
m2->mm_last_pg = txn->mt_next_pgno - 1;

// 3. 更新数据库信息
for (i = 0; i < CORE_DBS; i++) {
    m2->mm_dbs[i] = txn->mt_dbs[i];
}

// 4. 写入元数据页
{
#ifdef _WIN32
    // Windows: 使用专门的元数据文件句柄
    rc = WriteFile(env->me_mfd, meta, env->me_psize, &wsize, NULL);
#else
    // Unix: 使用 pwrite
    rc = pwrite(env->me_mfd, meta, env->me_psize,
                meta_num * env->me_psize);
#endif
}

// 5. 同步元数据页
if (!(txn->mt_flags & MDB_TXN_NOMETASYNC)) {
#ifdef _WIN32
    FlushFileBuffers(env->me_mfd);
#else
    if (env->me_flags & MDB_FSYNCONLY)
        fsync(env->me_mfd);
    else
        fdatasync(env->me_mfd);
#endif
}
```

### 元数据页的原子性

```
元数据页的原子性更新依赖于：

1. 校验和（可选编译）
   - 更新 mm_txnid 前计算校验和
   - 读取时验证校验和

2. mm_txnid 的特殊作用
   - 两个元数据页的 mm_txnid 奇偶性交替
   - 只有一个页的 mm_txnid 与 mti_txnid 匹配
   - 崩溃后选择较大的 mm_txnid

3. 写入顺序
   - 先写入元数据页
   - 再同步元数据页
   - 最后更新 mti_txnid（内存中）
```

---

## 7.5 同步策略

### 同步选项

```c
// 1. 完全同步（默认）
if (!(txn->mt_flags & (MDB_TXN_NOSYNC | MDB_TXN_NOMETASYNC))) {
    // 同步元数据
    fsync(env->me_mfd);

    // 如果有脏数据，也同步数据文件
    if (txn->mt_flags & MDB_TXN_DIRTY) {
        fsync(env->me_fd);
    }
}

// 2. MDB_NOMETASYNC：不同步元数据
else if (txn->mt_flags & MDB_TXN_NOMETASYNC) {
    // 只同步数据文件
    if (txn->mt_flags & MDB_TXN_DIRTY) {
        fsync(env->me_fd);
    }
}

// 3. MDB_NOSYNC：完全不同步
else if (txn->mt_flags & MDB_TXN_NOSYNC) {
    // 跳过所有同步
}
```

### 性能与安全性的权衡

```
同步策略           | 性能 | 数据安全性 | 崩溃恢复
------------------|------|-----------|----------
完全同步（默认）    | 低   | 高        | 完整
MDB_NOMETASYNC    | 中   | 中        | 可能丢失最近事务
MDB_NOSYNC        | 高   | 低        | 可能丢失多个事务
MDB_WRITEMAP      | 高   | 中        | 取决于其他标志
```

---

## 7.6 事务中止

### mdb_txn_abort() (mdb.c:3504)

```c
int mdb_txn_abort(MDB_txn *txn)
{
    return _mdb_txn_abort(txn);
}

static int _mdb_txn_abort(MDB_txn *txn)
{
    MDB_env *env;

    if (txn == NULL)
        return 0;

    // 1. 中止子事务
    if (txn->mt_child) {
        _mdb_txn_abort(txn->mt_child);
    }

    env = txn->mt_env;

    // 2. 读事务：注销读者槽位
    if (F_ISSET(txn->mt_flags, MDB_TXN_RDONLY)) {
        if (txn->mt_u.reader) {
            txn->mt_u.reader->mr_txnid = (txnid_t)-1;
        }
    }
    // 3. 写事务：丢弃所有修改
    else {
        // 所有修改的页面都会被丢弃
        // 因为它们没有被写入到元数据页

        // 释放写入者锁
        if (env->me_txn == txn) {
            env->me_txn = NULL;
            UNLOCK_MUTEX(env->me_wmutex);
        }
    }

    // 4. 清理资源
    mdb_txn_end(txn, MDB_END_FREE);

    return 0;
}
```

### 中止时的资源清理

```c
// mdb_txn_end() - 清理事务资源
static void mdb_txn_end(MDB_txn *txn, unsigned int mode)
{
    MDB_env *env = txn->mt_env;

    // 1. 关闭游标
    if (mode & MDB_END_UPDATE)
        mdb_cursors_close(txn, 0);

    // 2. 释放空闲页列表
    if (mode & MDB_END_FREE)
        mdb_midl_free(txn->mt_free_pgs);

    // 3. 释放脏页列表（如果不是写事务）
    if (!(txn->mt_flags & MDB_TXN_RDONLY) && (mode & MDB_END_DIRTY))
        mdb_midl_free(txn->mt_u.dirty_list);

    // 4. 释放读者槽位
    if (mode & MDB_END_SLOT) {
        if (txn->mt_u.reader) {
            txn->mt_u.reader->mr_pid = 0;
            txn->mt_u.reader->mr_txnid = 0;
        }
    }

    // 5. 设置已完成标志
    txn->mt_flags |= MDB_TXN_FINISHED;

    // 6. 释放事务结构（如果是动态分配的）
    if (txn != env->me_txn0)
        free(txn);
}
```

---

## 7.7 错误处理

### 事务错误标志

```c
#define MDB_TXN_ERROR  0x02

// 当发生错误时
if (rc != MDB_SUCCESS) {
    txn->mt_flags |= MDB_TXN_ERROR;

    // 如果是嵌套事务，父事务也标记为错误
    if (txn->mt_parent) {
        txn->mt_parent->mt_flags |= MDB_TXN_ERROR;
    }
}

// 检查事务是否可用
if (txn->mt_flags & MDB_TXN_ERROR) {
    return MDB_BAD_TXN;
}
```

### 常见错误

| 错误码 | 说明 | 处理方式 |
|--------|------|----------|
| MDB_TXN_FULL | 脏页列表满 | 中止事务或使用嵌套事务 |
| MDB_MAP_FULL | 映射空间满 | 增加映射大小 |
| MDB_RESERVATION_FAIL | 预留空间失败 | 中止事务 |
| MDB_BAD_TXN | 事务已损坏 | 中止并重新开始 |
| MDB_PANIC | 致命错误 | 关闭环境 |

---

## 7.8 今日练习

### 练习 1: 观察事务提交

```c
// 在事务提交前后打印元数据
void print_metadata(MDB_meta *meta) {
    printf("Meta: txnid=%llu, last_pg=%u\n",
           (unsigned long long)meta->mm_txnid,
           meta->mm_last_pg);
}

// 使用示例
MDB_txn *txn;
mdb_txn_begin(env, NULL, 0, &txn);

print_metadata(env->me_metas[0]);
print_metadata(env->me_metas[1]);

// ... 执行操作 ...

mdb_txn_commit(txn);

print_metadata(env->me_metas[0]);
print_metadata(env->me_metas[1]);
```

### 练习 2: 测试同步策略

```c
// 比较不同同步策略的性能
void test_sync_strategies(MDB_env *env) {
    struct timespec start, end;
    int n = 10000;

    // 测试 1: 完全同步
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < n; i++) {
        MDB_txn *txn;
        mdb_txn_begin(env, NULL, 0, &txn);
        // ... 执行操作 ...
        mdb_txn_commit(txn);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("Full sync: %ld ns\n", diff(start, end));

    // 测试 2: MDB_NOSYNC
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < n; i++) {
        MDB_txn *txn;
        mdb_txn_begin(env, NULL, MDB_NOSYNC, &txn);
        // ... 执行操作 ...
        mdb_txn_commit(txn);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("NOSYNC: %ld ns\n", diff(start, end));
}
```

### 练习 3: 事务中止测试

```c
// 测试中止是否回滚所有修改
void test_abort(MDB_env *env) {
    MDB_txn *txn;
    MDB_val key, data;

    // 开始事务
    mdb_txn_begin(env, NULL, 0, &txn);

    // 插入数据
    key = (MDB_val){"test", 4};
    data = (MDB_val){"data", 4};
    mdb_put(txn, dbi, &key, &data, 0);

    // 中止事务
    mdb_txn_abort(txn);

    // 验证数据是否不存在
    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    int rc = mdb_get(txn, dbi, &key, &data);
    if (rc == MDB_NOTFOUND) {
        printf("Abort successful: data not found\n");
    }
    mdb_txn_abort(txn);
}
```

---

## 7.9 完整示例：事务提交分析器

### commit_analyzer.c - 事务提交监控工具

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lmdb.h>
#include <time.h>
#include <sys/time.h>

// 提交统计
typedef struct {
    int num_commits;
    double total_time;
    double max_time;
    double min_time;
    size_t total_dirty_pages;
    size_t total_bytes;
} CommitStats;

static CommitStats stats = {
    .num_commits = 0,
    .total_time = 0,
    .max_time = 0,
    .min_time = 1e9,
    .total_dirty_pages = 0,
    .total_bytes = 0
};

// 获取当前时间（微秒）
double get_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000.0 + tv.tv_usec;
}

// 模拟事务提交并统计
int timed_commit(MDB_txn *txn) {
    double start = get_time_us();
    int rc = mdb_txn_commit(txn);
    double end = get_time_us();
    double elapsed = (end - start) / 1000000.0;  // 转换为秒

    stats.num_commits++;
    stats.total_time += elapsed;

    if (elapsed > stats.max_time) stats.max_time = elapsed;
    if (elapsed < stats.min_time) stats.min_time = elapsed;

    return rc;
}

// 打印统计信息
void print_stats() {
    printf("\n========== 事务提交统计 ==========\n");
    printf("提交次数:       %d\n", stats.num_commits);
    printf("总耗时:         %.3f 秒\n", stats.total_time);
    printf("平均耗时:       %.6f 秒\n", stats.total_time / stats.num_commits);
    printf("最大耗时:       %.6f 秒\n", stats.max_time);
    printf("最小耗时:       %.6f 秒\n", stats.min_time);
    printf("总脏页数:       %zu\n", stats.total_dirty_pages);
    printf("总写入字节:     %zu\n", stats.total_bytes);

    if (stats.total_bytes > 0) {
        double throughput = stats.total_bytes / stats.total_time;
        printf("吞吐量:         %.2f MB/s\n", throughput / (1024*1024));
    }
    printf("===================================\n\n");
}

// 基准测试：不同同步策略
void benchmark_sync_modes() {
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;
    int rc;
    int n = 1000;

    const char *modes[] = {"默认同步", "MDB_NOSYNC", "MDB_NOMETASYNC"};
    unsigned int flags[] = {0, MDB_NOSYNC, MDB_NOMETASYNC};

    for (int i = 0; i < 3; i++) {
        // 创建环境
        mdb_env_create(&env);
        mdb_env_set_mapsize(env, 1024 * 1024 * 100);
        char path[64];
        snprintf(path, sizeof(path), "./bench_sync_%d", i);
        system("rm -rf bench_sync_*");
        mkdir(path, 0755);
        mdb_env_open(env, path, flags[i], 0664);

        // 重置统计
        memset(&stats, 0, sizeof(stats));
        stats.min_time = 1e9;

        double start = get_time_us();

        for (int j = 0; j < n; j++) {
            mdb_txn_begin(env, NULL, flags[i], &txn);
            mdb_dbi_open(txn, NULL, 0, &dbi);

            char kbuf[32], dbuf[64];
            snprintf(kbuf, sizeof(kbuf), "key-%d", j);
            snprintf(dbuf, sizeof(dbuf), "value-%d", j);

            key.mv_data = kbuf;
            key.mv_size = strlen(kbuf);
            data.mv_data = dbuf;
            data.mv_size = strlen(dbuf);

            mdb_put(txn, dbi, &key, &data, 0);
            timed_commit(txn);
        }

        double end = get_time_us();
        double total = (end - start) / 1000000.0;

        printf("\n========== %s ==========\n", modes[i]);
        printf("总耗时:         %.3f 秒\n", total);
        printf("吞吐量:         %.0f TPS\n", n / total);
        print_stats();

        mdb_dbi_close(env, dbi);
        mdb_env_close(env);
    }
}

// 基准测试：批量大小 vs 提交次数
void benchmark_batch_sizes() {
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;

    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024 * 100);
    system("rm -rf bench_batch && mkdir -p bench_batch");
    mdb_env_open(env, "./bench_batch", MDB_NOSYNC, 0664);

    int batch_sizes[] = {1, 10, 100, 1000};
    int num_batches = sizeof(batch_sizes) / sizeof(batch_sizes[0]);

    printf("\n========== 批量大小基准测试 ==========\n");

    for (int i = 0; i < num_batches; i++) {
        int batch_size = batch_sizes[i];
        int total_ops = 10000;
        int num_commits = total_ops / batch_size;

        memset(&stats, 0, sizeof(stats));
        stats.min_time = 1e9;

        double start = get_time_us();

        for (int j = 0; j < num_commits; j++) {
            mdb_txn_begin(env, NULL, MDB_NOSYNC, &txn);
            mdb_dbi_open(txn, NULL, 0, &dbi);

            for (int k = 0; k < batch_size; k++) {
                char kbuf[32], dbuf[64];
                int op = j * batch_size + k;
                snprintf(kbuf, sizeof(kbuf), "key-%d", op);
                snprintf(dbuf, sizeof(dbuf), "value-%d", op);

                key.mv_data = kbuf;
                key.mv_size = strlen(kbuf);
                data.mv_data = dbuf;
                data.mv_size = strlen(dbuf);

                mdb_put(txn, dbi, &key, &data, 0);
            }
            timed_commit(txn);
        }

        double end = get_time_us();
        double total = (end - start) / 1000000.0;

        printf("\n批量大小: %4d | 提交次数: %4d | 总耗时: %.3fs | TPS: %.0f\n",
               batch_size, num_commits, total, total_ops / total);
        printf("  平均提交耗时: %.6fs\n", stats.total_time / stats.num_commits);
    }

    printf("\n========================================\n");

    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}

int main() {
    printf("LMDB 事务提交基准测试工具\n");
    printf("==========================\n");

    benchmark_sync_modes();
    benchmark_batch_sizes();

    return 0;
}
```

---

## 7.10 常见问题解答

### Q1: 为什么元数据页需要交替更新？

**A:** 元数据页交替更新是为了实现原子性和崩溃恢复：

```
两个元数据页：meta[0] 和 meta[1]

写事务1 (txnid=1):
  - 更新 meta[1].mm_txnid = 1
  - 同步 meta[1] 到磁盘
  - 更新 mti_txnid = 1

写事务2 (txnid=2):
  - 更新 meta[0].mm_txnid = 2
  - 同步 meta[0] 到磁盘
  - 更新 mti_txnid = 2

崩溃恢复时:
  - 读取两个元数据页
  - 选择 mm_txnid 较大的页
  - 如果其中一个损坏，使用另一个
```

### Q2: writev() 比多次 write() 有什么优势？

**A:** `writev()` 的优势：

1. **减少系统调用**：一次调用写入多个缓冲区
2. **原子性更好**：对于连续页面，一次写入更可靠
3. **减少上下文切换**：用户态/内核态切换次数减少
4. **性能提升**：批量写入充分利用磁盘带宽

```
性能对比（写入100个页面）：

多次 write():
  100次系统调用
  100次上下文切换
  ~5000 CPU 周期

单次 writev():
  1次系统调用
  1次上下文切换
  ~500 CPU 周期

性能提升: ~10倍
```

### Q3: 嵌套事务提交时，脏页如何合并到父事务？

**A:** 合并过程：

1. **脏页列表合并**：子事务的脏页添加到父事务的脏页列表
2. **空闲页合并**：子事务释放的页面合并到父事务的空闲列表
3. **数据库表更新**：子事务的数据库状态复制到父事务
4. **溢出页处理**：子事务的溢出页重新标记为父事务的脏页
5. **清除标志**：清除父事务的 `MDB_TXN_HAS_CHILD` 标志

### Q4: MDB_NOSYNC 标志下，崩溃后数据会丢失多少？

**A:** 取决于操作系统和磁盘缓存：

```
最坏情况：
  - 数据在操作系统的页缓存中
  - 崩溃导致页缓存丢失
  - 可能丢失所有未同步的写事务

典型情况：
  - Linux: 几秒到30秒的数据（取决于脏页回写策略）
  - macOS: 类似 Linux
  - Windows: 取决于卷的缓存设置

恢复后：
  - 数据库保证一致性
  - 只丢失最近提交的数据
  - 不会有损坏的数据
```

### Q5: 什么时候应该使用嵌套事务？

**A:** 嵌套事务的适用场景：

```
适用场景：
1. 需要原子性地执行多个操作
   - 所有子操作成功，父事务才提交
   - 任何一个失败，全部回滚

2. 复杂的数据修改逻辑
   - 需要临时状态
   - 可能需要回滚

3. 资源密集型操作
   - 分批处理大量数据
   - 每批作为子事务

不适用场景：
1. 简单的增删改查
2. 对性能要求极高的场景（嵌套有额外开销）
3. 不需要原子性保证的操作
```

---

## 7.11 今日练习

1. **编译运行分析器**：
   ```bash
   gcc -o commit_analyzer commit_analyzer.c -llmdb -lpthread
   ./commit_analyzer
   ```

2. **观察同步策略影响**：
   - 比较不同同步策略的性能差异
   - 理解性能与安全性的权衡

3. **调试提交流程**：
   ```bash
   gdb ./mtest
   (gdb) break mdb_txn_commit
   (gdb) run
   (gdb) print *txn
   (gdb) step
   ```

---

## 7.12 思考题

1. 为什么元数据页需要交替更新？
2. writev() 比多次 write() 有什么优势？
3. 嵌套事务提交时，脏页如何合并到父事务？
4. MDB_NOSYNC 标志下，崩溃后数据会丢失多少？
5. 如何在性能和数据安全之间取得平衡？

---

## 明天预告

Day 8 将深入讲解 MVCC（多版本并发控制）。我们将学习：
- MVCC 的原理
- 版本管理机制
- 读事务的一致性保证
- 写事务的隔离性

MVCC 是 LMDB 读写不阻塞的关键！

---
**参考文献：**
- mdb.c: 3975-4210 (_mdb_txn_commit, mdb_txn_commit)
- mdb.c: 3492-3510 (_mdb_txn_abort, mdb_txn_abort)
- mdb.c: 1253-1280 (MDB_meta)
