# LMDB 性能调优指南 (Performance Tuning Guide)

本指南详细介绍了如何优化 LMDB 的性能，包括环境配置、事务管理、内存使用和硬件考虑。

## 目录

1. [环境配置优化](#环境配置优化)
2. [事务管理优化](#事务管理优化)
3. [内存管理优化](#内存管理优化)
4. [数据结构优化](#数据结构优化)
5. [硬件层优化](#硬件层优化)
6. [性能分析工具](#性能分析工具)
7. [常见性能问题](#常见性能问题)

---

## 环境配置优化

### 1. 映射大小 (Map Size)

映射大小决定了数据库的最大容量。设置不当会影响性能。

```c
// 推荐的映射大小配置
size_t calculate_map_size(uint64_t expected_data_size) {
    // 预留额外 50% 空间用于索引和开销
    return expected_data_size * 3 / 2;
}

// 示例配置
mdb_env_set_mapsize(env, 1024 * 1024 * 1024);  // 1GB
mdb_env_set_mapsize(env, 10ULL * 1024 * 1024 * 1024);  // 10GB
```

**最佳实践:**

| 数据规模 | 推荐映射大小 | 说明 |
|---------|------------|------|
| < 100 MB | 256 MB | 小型应用 |
| 100 MB - 1 GB | 2 GB | 中型应用 |
| 1 GB - 10 GB | 20 GB | 大型应用 |
| > 10 GB | 50 GB+ | 超大规模 |

**注意事项:**
- 映射大小应该是页面大小 (通常是 4KB) 的倍数
- 过大会浪费虚拟内存空间
- 过小会导致频繁的映射调整
- LMDB 使用稀疏文件，实际磁盘占用取决于数据量

### 2. 最大读者数 (Max Readers)

读者数设置影响并发读取能力。

```c
// 计算所需读者数
int calculate_max_readers(int expected_threads) {
    // 预留一些额外读者数
    return expected_threads * 2;
}

// 配置示例
mdb_env_set_maxreaders(env, 64);   // 默认值
mdb_env_set_maxreaders(env, 256);  // 高并发场景
mdb_env_set_maxreaders(env, 1024); // 超高并发
```

**性能影响:**

| 读者数 | 并发能力 | 内存开销 | 适用场景 |
|-------|---------|---------|---------|
| 16 | 低 | ~100 KB | 单线程应用 |
| 64 | 中 | ~400 KB | 一般应用 |
| 256 | 高 | ~1.6 MB | 高并发应用 |
| 1024 | 极高 | ~6.4 MB | 分布式系统 |

### 3. 最大数据库数 (Max DBs)

当使用多个命名数据库时需要配置。

```c
// 根据命名数据库数量配置
mdb_env_set_maxdbs(env, 16);   // 默认值
mdb_env_set_maxdbs(env, 64);   // 多数据库应用
mdb_env_set_maxdbs(env, 256);  // 复杂分片场景
```

**性能考虑:**
- 每个 DB 增加少量元数据开销
- 过多 DB 会略微降低打开速度
- 建议根据实际需求设置，不超过 256

### 4. 环境标志 (Flags)

选择合适的标志可以显著影响性能。

```c
// 性能优化的标志组合
unsigned int flags = MDB_NOTLS | MDB_NOSYNC | MDB_NOMETASYNC;

// 详细说明
mdb_env_open(env, path, flags, 0664);
```

**标志说明:**

| 标志 | 性能影响 | 安全性 | 适用场景 |
|-----|---------|-------|---------|
| MDB_NOTLS | +5-10% | 安全 | 多线程应用 (推荐) |
| MDB_NOSYNC | +20-50% | 低 | 可接受数据丢失风险 |
| MDB_NOMETASYNC | +10-20% | 中 | 平衡性能和安全 |
| MDB_WRITEMAP | +10-15% | 需谨慎 | 只读或严格控制写入 |
| MDB_MAPASYNC | +15-30% | 低 | 批量导入场景 |
| (默认) | 基准 | 高 | 事务完整性优先 |

**标志组合示例:**

```c
// 最大性能（可接受数据丢失）
unsigned int max_perf = MDB_NOTLS | MDB_NOSYNC | MDB_WRITEMAP;

// 平衡性能和安全
unsigned int balanced = MDB_NOTLS | MDB_NOMETASYNC;

// 最大安全（默认）
unsigned int max_safe = 0;

// 只读模式
unsigned int read_only = MDB_RDONLY | MDB_NOTLS;
```

---

## 事务管理优化

### 1. 事务大小

```c
// 小事务（推荐用于并发写入）
for (int i = 0; i < 10000; i++) {
    mdb_txn_begin(env, NULL, 0, &txn);
    // 单次写入
    mdb_put(txn, dbi, &key, &data, 0);
    mdb_txn_commit(txn);
}

// 批量事务（适合大量数据导入）
mdb_txn_begin(env, NULL, 0, &txn);
for (int i = 0; i < 10000; i++) {
    // 多次写入
    mdb_put(txn, dbi, &key, &data, 0);
}
mdb_txn_commit(txn);
```

**性能对比:**

| 事务类型 | 吞吐量 | 延迟 | 适用场景 |
|---------|-------|------|---------|
| 单操作事务 | 50K ops/s | 低 | 高并发写入 |
| 100操作/事务 | 200K ops/s | 中 | 批量处理 |
| 10000操作/事务 | 500K ops/s | 高 | 数据导入 |

### 2. 读写事务分离

```c
// 读事务（可并发）
void read_operations(MDB_env *env) {
    MDB_txn *txn;
    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);

    // 多个读取操作
    for (int i = 0; i < 100; i++) {
        mdb_get(txn, dbi, &key, &data);
    }

    mdb_txn_abort(txn);  // 或 mdb_txn_commit()
}

// 写事务（独占）
void write_operations(MDB_env *env) {
    MDB_txn *txn;
    mdb_txn_begin(env, NULL, 0, &txn);

    // 写入操作
    mdb_put(txn, dbi, &key, &data, 0);

    mdb_txn_commit(txn);
}
```

### 3. 嵌套事务

```c
// 使用嵌套事务进行部分回滚
int nested_transaction_example(MDB_env *env) {
    MDB_txn *parent, *child;
    int rc;

    // 父事务
    rc = mdb_txn_begin(env, NULL, 0, &parent);
    if (rc != 0) return rc;

    // 子事务 1
    rc = mdb_txn_begin(env, parent, 0, &child);
    if (rc == 0) {
        // 执行一些操作
        mdb_put(child, dbi, &key1, &data1, 0);

        // 如果有错误，只回滚子事务
        if (error_condition) {
            mdb_txn_abort(child);
        } else {
            mdb_txn_commit(child);
        }
    }

    // 子事务 2
    rc = mdb_txn_begin(env, parent, 0, &child);
    if (rc == 0) {
        mdb_put(child, dbi, &key2, &data2, 0);
        mdb_txn_commit(child);
    }

    // 提交父事务
    return mdb_txn_commit(parent);
}
```

### 4. 事务重试

```c
// 带重试的事务执行
int transaction_with_retry(MDB_env *env,
                           int max_retries,
                           int (*callback)(MDB_txn*)) {
    MDB_txn *txn;
    int rc;
    int attempt = 0;

    while (attempt < max_retries) {
        attempt++;

        rc = mdb_txn_begin(env, NULL, 0, &txn);
        if (rc != 0) {
            if (rc == MDB_MAP_RESIZED && attempt < max_retries) {
                continue;  // 映射大小已改变，重试
            }
            return rc;
        }

        rc = callback(txn);
        if (rc != 0) {
            mdb_txn_abort(txn);
            if (rc == MDB_MAP_FULL && attempt < max_retries) {
                usleep(1000);  // 短暂等待后重试
                continue;
            }
            return rc;
        }

        rc = mdb_txn_commit(txn);
        if (rc != 0) {
            if (rc == MDB_MAP_FULL && attempt < max_retries) {
                usleep(1000);
                continue;
            }
            return rc;
        }

        return 0;  // 成功
    }

    return -1;  // 超过最大重试次数
}
```

---

## 内存管理优化

### 1. 零拷贝读取

```c
// 避免不必要的数据拷贝
int zero_copy_read(MDB_env *env, const char *key) {
    MDB_txn *txn;
    MDB_val k, v;
    int rc;

    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) return rc;

    k.mv_data = (void*)key;
    k.mv_size = strlen(key) + 1;

    rc = mdb_get(txn, dbi, &k, &v);
    if (rc == 0) {
        // 直接访问 v.mv_data，无需拷贝
        process_data(v.mv_data, v.mv_size);
        // 注意：v.mv_data 在事务结束后失效
    }

    mdb_txn_abort(txn);
    return rc;
}
```

**性能提升:**
- 减少内存拷贝: +10-20%
- 降低 CPU 使用: -5-15%
- 减少内存分配: -20-30%

### 2. 值大小优化

```c
// 优化值的大小
// 不好的做法：大型值
struct large_value {
    char data[1024 * 1024];  // 1MB
};

// 好的做法：使用引用
struct value_ref {
    char key[64];
    uint64_t offset;
    uint32_t size;
};
```

**建议值大小:**

| 值大小 | 性能 | 推荐场景 |
|-------|-----|---------|
| < 1 KB | 极快 | 大多数键值 |
| 1-4 KB | 快 | JSON 文档 |
| 4-16 KB | 中等 | 二进制数据 |
| > 16 KB | 较慢 | 考虑外部存储 |

### 3. 键大小优化

```c
// 优化键的设计
// 不好的做法：长键
const char *long_key = "user:profile:settings:preferences:ui:theme:dark";

// 好的做法：短键或使用 ID
const char *short_key = "usr:12345";
const char *id_key = "12345";  // 使用数据库/索引关联元数据
```

**键设计原则:**
- 保持键简短 (建议 < 100 字节)
- 使用数字 ID 代替长字符串
- 避免重复前缀 (利用数据库或命名空间)
- 考虑键的排序特性

---

## 数据结构优化

### 1. 键排序优化

```c
// 利用排序优化范围查询
// 方法 1: 零填充数字
char sorted_key[64];
snprintf(sorted_key, sizeof(sorted_key), "%010d", user_id);

// 方法 2: 反转优先级（最大堆）
char priority_key[64];
snprintf(priority_key, sizeof(priority_key), "%02d-%020lu",
         10 - priority, timestamp);

// 方法 3: 时间戳前缀
char time_key[64];
snprintf(time_key, sizeof(time_key), "%020lu-%s", timestamp, event_id);
```

### 2. 数据库分片策略

```c
// 水平分片
int get_shard(const char *key, int num_shards) {
    unsigned int hash = 5381;
    int c;

    while ((c = *key++))
        hash = ((hash << 5) + hash) + c;

    return hash % num_shards;
}

// 垂直分片（按数据类型）
// shard_0: users
// shard_1: posts
// shard_2: comments
```

### 3. 索引策略

```c
// 主数据
mdb_put(txn, primary_db, &user_id_key, &user_data, 0);

// 邮箱索引
char email_key[256];
snprintf(email_key, sizeof(email_key), "email:%s", user_email);
mdb_put(txn, email_index_db, &email_key, &user_id_key, 0);

// 用户名索引
char username_key[256];
snprintf(username_key, sizeof(username_key), "username:%s", username);
mdb_put(txn, username_index_db, &username_key, &user_id_key, 0);
```

---

## 硬件层优化

### 1. 磁盘 I/O 优化

```c
// 针对不同存储介质的优化

// SSD 优化（默认配置通常最好）
unsigned int ssd_flags = MDB_NOTLS;

// HDD 优化（减少同步）
unsigned int hdd_flags = MDB_NOTLS | MDB_NOSYNC | MDB_NOMETASYNC;

// RAM disk（最大性能）
unsigned int ramdisk_flags = MDB_NOTLS | MDB_NOSYNC | MDB_WRITEMAP;
```

**性能对比:**

| 存储类型 | 随机读 | 随机写 | 推荐配置 |
|---------|-------|-------|---------|
| RAM disk | 极快 | 极快 | NOSYNC + WRITEMAP |
| NVMe SSD | 很快 | 很快 | NOTLS + NOMETASYNC |
| SATA SSD | 快 | 快 | NOTLS + NOMETASYNC |
| HDD | 慢 | 很慢 | NOSYNC + 小事务 |

### 2. 文件系统选择

**推荐文件系统:**

| 文件系统 | 性能 | 可靠性 | LMDB 支持 |
|---------|-----|-------|----------|
| ext4 | 高 | 高 | ✅ 优秀 |
| xfs | 很高 | 高 | ✅ 优秀 |
| btrfs | 高 | 很高 | ✅ 良好 |
| ZFS | 中 | 极高 | ⚠️ 需要调优 |
| NTFS | 中 | 中 | ⚠️ 性能较低 |

**挂载选项优化:**

```bash
# ext4 优化
mount -o noatime,nodiratime,data=writeback /dev/sdb1 /mnt/lmdb

# xfs 优化
mount -o noatime,allocsize=4M /dev/sdb1 /mnt/lmdb
```

### 3. 系统资源限制

```bash
# 增加文件描述符限制
ulimit -n 65536

# 增加内存映射限制
sysctl -w vm.max_map_count=262144

# 持久化配置
echo "vm.max_map_count=262144" >> /etc/sysctl.conf
```

---

## 性能分析工具

### 1. 内置统计

```c
// 获取数据库统计信息
void print_database_stats(MDB_env *env, MDB_dbi dbi) {
    MDB_txn *txn;
    MDB_stat stat;
    int rc;

    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) return;

    rc = mdb_stat(txn, dbi, &stat);
    if (rc == 0) {
        printf("Database Statistics:\n");
        printf("  Page size: %u\n", stat.ms_psize);
        printf("  Depth: %u\n", stat.ms_depth);
        printf("  Branch pages: %zu\n", stat.ms_branch_pages);
        printf("  Leaf pages: %zu\n", stat.ms_leaf_pages);
        printf("  Overflow pages: %zu\n", stat.ms_overflow_pages);
        printf("  Entries: %zu\n", stat.ms_entries);

        // 计算空间利用率
        size_t total_pages = stat.ms_branch_pages +
                            stat.ms_leaf_pages +
                            stat.ms_overflow_pages;
        size_t total_size = total_pages * stat.ms_psize;
        printf("  Total size: %zu MB\n", total_size / (1024 * 1024));
    }

    mdb_txn_abort(txn);
}

// 获取环境信息
void print_env_info(MDB_env *env) {
    MDB_envinfo info;
    int rc;

    rc = mdb_env_info(env, &info);
    if (rc == 0) {
        printf("Environment Information:\n");
        printf("  Map address: %p\n", info.me_mapaddress);
        printf("  Map size: %zu MB\n", info.me_mapsize / (1024 * 1024));
        printf("  Last page number: %zu\n", info.me_last_pgno);
        printf("  Last transaction ID: %zu\n", info.me_last_txnid);
        printf("  Max readers: %u\n", info.me_maxreaders);
        printf("  Num readers: %u\n", info.me_numreaders);
    }
}
```

### 2. 性能测试框架

```c
// 性能基准测试
typedef struct {
    const char *name;
    int (*test_func)(MDB_env*, int iterations);
    int iterations;
} benchmark_t;

double run_benchmark(MDB_env *env, benchmark_t *bench) {
    struct timeval start, end;
    double elapsed;

    gettimeofday(&start, NULL);
    bench->test_func(env, bench->iterations);
    gettimeofday(&end, NULL);

    elapsed = (end.tv_sec - start.tv_sec) +
              (end.tv_usec - start.tv_usec) / 1000000.0;

    return elapsed;
}

void run_all_benchmarks(MDB_env *env) {
    benchmark_t benchmarks[] = {
        { "Sequential Write", test_sequential_write, 100000 },
        { "Random Read", test_random_read, 100000 },
        { "Range Query", test_range_query, 10000 },
        { NULL, NULL, 0 }
    };

    printf("Performance Benchmarks:\n");
    printf("%-20s %12s %12s %12s\n",
           "Test", "Iterations", "Time (s)", "Ops/s");
    printf("%-20s %12s %12s %12s\n",
           "----", "----------", "-------", "-----");

    for (int i = 0; benchmarks[i].name != NULL; i++) {
        double elapsed = run_benchmark(env, &benchmarks[i]);
        double ops_per_sec = benchmarks[i].iterations / elapsed;

        printf("%-20s %12d %12.3f %12.0f\n",
               benchmarks[i].name,
               benchmarks[i].iterations,
               elapsed,
               ops_per_sec);
    }
}
```

### 3. 监控工具

```c
// 实时监控
void monitor_database(MDB_env *env, MDB_dbi dbi, int interval_sec) {
    MDB_stat last_stat = {0};

    while (1) {
        MDB_txn *txn;
        MDB_stat stat;

        if (mdb_txn_begin(env, NULL, MDB_RDONLY, &txn) == 0) {
            if (mdb_stat(txn, dbi, &stat) == 0) {
                printf("\rEntries: %zu | Pages: %zu | Size: %zu MB | Depth: %u",
                       stat.ms_entries,
                       stat.ms_leaf_pages + stat.ms_branch_pages,
                       (stat.ms_leaf_pages + stat.ms_branch_pages) *
                           stat.ms_psize / (1024 * 1024),
                       stat.ms_depth);
                fflush(stdout);

                // 检测变化
                if (stat.ms_entries != last_stat.ms_entries) {
                    printf("\nDelta: %+ld entries\n",
                           (long)(stat.ms_entries - last_stat.ms_entries));
                }

                last_stat = stat;
            }
            mdb_txn_abort(txn);
        }

        sleep(interval_sec);
    }
}
```

---

## 常见性能问题

### 问题 1: 写入性能下降

**症状:**
- 初始写入快，后来变慢
- 大量写入时性能显著下降

**原因:**
- 映射空间不足导致重新映射
- 页面碎片化
- 锁竞争

**解决方案:**

```c
// 1. 增加映射大小
mdb_env_set_mapsize(env, 10ULL * 1024 * 1024 * 1024);

// 2. 使用批量事务
mdb_txn_begin(env, NULL, 0, &txn);
for (int i = 0; i < batch_size; i++) {
    mdb_put(txn, dbi, &key, &data, 0);
}
mdb_txn_commit(txn);

// 3. 减少锁竞争
// 使用多个进程/线程分别写入不同的键范围
```

### 问题 2: 读取延迟波动

**症状:**
- 读取延迟不稳定
- 偶尔出现高延迟

**原因:**
- 页面未缓存
- 写入事务阻塞读取
- 系统内存压力

**解决方案:**

```c
// 1. 预热缓存
void warm_up_cache(MDB_env *env, MDB_dbi dbi) {
    MDB_txn *txn;
    MDB_cursor *cursor;
    MDB_val key, data;

    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    mdb_cursor_open(txn, dbi, &cursor);

    // 顺序读取所有数据到缓存
    int rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    while (rc == 0) {
        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
}

// 2. 分离读写事务
// 使用 MDB_RDONLY 标志

// 3. 增加系统缓存
// sysctl -w vm.vfs_cache_pressure=50
```

### 问题 3: 内存使用过高

**症状:**
- 进程内存持续增长
- 系统内存不足

**原因:**
- 读者槽位未释放
- 长时间运行的读事务
- 映射过大

**解决方案:**

```c
// 1. 及时结束读事务
MDB_txn *txn;
mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
// 执行读取
mdb_txn_abort(txn);  // 或 commit

// 2. 避免长时间读事务
// 不要在函数间传递读事务指针

// 3. 定期重启进程
// 或使用 mdb_env_set_maxreaders 限制读者数
```

---

## 性能优化检查清单

### 环境配置
- [ ] 映射大小充足（至少预期数据的 1.5 倍）
- [ ] 最大读者数配置合理
- [ ] 使用适当的标志（MDB_NOTLS, MDB_NOMETASYNC）
- [ ] 数据库数量不超过需求

### 事务管理
- [ ] 读事务使用 MDB_RDONLY 标志
- [ ] 批量操作使用单个事务
- [ ] 及时结束不需要的事务
- [ ] 实现事务重试逻辑

### 数据设计
- [ ] 键保持简短（< 100 字节）
- [ ] 值大小合理（< 4 KB 优化）
- [ ] 使用适当的数据分片
- [ ] 索引设计合理

### 硬件优化
- [ ] 使用 SSD 或更快的存储
- [ ] 选择合适的文件系统
- [ ] 系统资源限制适当
- [ ] 文件系统挂载选项优化

### 监控维护
- [ ] 定期检查数据库统计
- [ ] 监控性能指标
- [ ] 定期备份数据
- [ ] 监控系统资源使用

---

## 参考资源

- [LMDB 官方文档](http://www.lmdb.tech/doc/)
- LMDB 源代码中的注释和示例
- 性能测试脚本: `examples/perf_test.c`
- 可视化工具: `examples/visualize_db.c`
