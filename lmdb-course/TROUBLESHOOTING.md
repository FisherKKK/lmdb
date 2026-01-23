# LMDB 故障排除指南 (Troubleshooting Guide)

本指南提供了 LMDB 常见问题的诊断方法和解决方案。

## 目录

1. [错误代码参考](#错误代码参考)
2. [常见错误及解决方案](#常见错误及解决方案)
3. [性能问题诊断](#性能问题诊断)
4. [数据恢复](#数据恢复)
5. [调试工具](#调试工具)
6. [最佳实践](#最佳实践)

---

## 错误代码参考

### 核心错误代码

```c
// 常见 LMDB 错误代码
#define MDB_KEYEXIST           -30799  // 键已存在
#define MDB_NOTFOUND           -30798  // 键未找到
#define MDB_PAGE_NOTFOUND      -30797  // 页面未找到
#define MDB_CORRUPTED          -30796  // 数据库已损坏
#define MDB_PANIC              -30795  // 致命错误
#define MDB_VERSION_MISMATCH   -30794  // 版本不匹配
#define MDB_INVALID            -30793  // 参数无效
#define MDB_MAP_FULL           -30792  // 映射已满
#define MDB_DBS_FULL           -30791  // 数据库已满
#define MDB_READERS_FULL       -30790  // 读者已满
#define MDB_TLS_FULL           -30789  // TLS 已满
#define MDB_TXN_FULL           -30788  // 事务已满
#define MDB_CURSOR_FULL        -30787  // 游标已满
#define MDB_PAGE_FULL          -30786  // 页面已满
#define MDB_MAP_RESIZED        -30785  // 映射大小已改变
#define MDB_INCOMPATIBLE       -30784  // 不兼容的操作
#define MDB_BAD_RSLOT          -30783  // 错误的读者槽位
#define MDB_BAD_TXN            -30782  // 错误的事务
#define MDB_BAD_VALSIZE        -30781  // 错误的值大小
#define MDB_BAD_DBI            -30780  // 错误的数据库
#define EACCES                 13      // 权限拒绝
#define EINVAL                 22      // 参数无效
#define ENOMEM                 12      // 内存不足
#define EIO                    5       // I/O 错误
```

### 错误处理宏

```c
// 完整的错误处理宏
#define CHECK_RC(rc, label, msg) \
    do { \
        if (rc != 0) { \
            fprintf(stderr, "Error: %s (code: %d)\n", msg, rc); \
            fprintf(stderr, "LMDB Error: %s\n", mdb_strerror(rc)); \
            goto label; \
        } \
    } while(0)

// 使用示例
int safe_operation(MDB_env *env) {
    MDB_txn *txn = NULL;
    int rc;

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    CHECK_RC(rc, cleanup, "Failed to begin transaction");

    // ... 执行操作 ...

    rc = mdb_txn_commit(txn);
    CHECK_RC(rc, cleanup, "Failed to commit transaction");

    return 0;

cleanup:
    if (txn) mdb_txn_abort(txn);
    return rc;
}
```

---

## 常见错误及解决方案

### 1. MDB_MAP_FULL (-30792)

**错误信息:**
```
mdb_txn_begin: MDB_MAP_FULL: Environment mapsize limit reached
```

**原因:**
- 数据库大小超过了预设的映射大小
- 事务无法分配新页面

**解决方案:**

```c
// 方案 1: 重新打开环境并增大映射大小
int expand_map_size(MDB_env *env, const char *path, size_t new_size) {
    // 关闭旧环境
    mdb_env_close(env);

    // 重新创建环境
    int rc = mdb_env_create(&env);
    if (rc != 0) return rc;

    // 设置更大的映射大小
    rc = mdb_env_set_mapsize(env, new_size);
    if (rc != 0) {
        mdb_env_close(env);
        return rc;
    }

    // 重新打开
    return mdb_env_open(env, path, 0, 0664);
}

// 方案 2: 初始设置时预留足够空间
mdb_env_set_mapsize(env, 10ULL * 1024 * 1024 * 1024);  // 10GB

// 方案 3: 使用稀疏文件
// Linux 自动使用稀疏文件，无需特殊配置
```

**预防措施:**
- 初始映射大小设置为预期数据量的 2-3 倍
- 定期监控数据库大小增长
- 实现监控脚本在接近限制时报警

### 2. MDB_READERS_FULL (-30790)

**错误信息:**
```
mdb_txn_begin: MDB_READERS_FULL: Environment maxreaders limit reached
```

**原因:**
- 并发读事务数量超过配置的最大读者数
- 读事务未正确关闭

**解决方案:**

```c
// 方案 1: 增加最大读者数
mdb_env_set_maxreaders(env, 256);  // 从默认 64 增加到 256

// 方案 2: 检查并修复未关闭的读事务
void cleanup_reader_slots(MDB_env *env) {
    // 自动清理机制会处理僵死的读者槽位
    // 但需要新的读事务触发清理

    MDB_txn *txn;
    // 触发清理
    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    mdb_txn_abort(txn);
}

// 方案 3: 确保读事务正确关闭
// 不好的做法
MDB_txn *txn;
mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
// 使用事务后忘记关闭

// 好的做法
MDB_txn *txn;
mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
// 使用事务
mdb_txn_abort(txn);  // 或 mdb_txn_commit(txn)
```

**诊断代码:**

```c
// 检查读者槽位使用情况
void check_reader_usage(MDB_env *env) {
    MDB_envinfo info;
    int rc = mdb_env_info(env, &info);

    if (rc == 0) {
        printf("Reader slots: %u / %u used (%.1f%%)\n",
               info.me_numreaders,
               info.me_maxreaders,
               100.0 * info.me_numreaders / info.me_maxreaders);

        if (info.me_numreaders >= info.me_maxreaders * 0.8) {
            printf("WARNING: Reader slots nearly full!\n");
        }
    }
}
```

### 3. MDB_KEYEXIST (-30799)

**错误信息:**
```
mdb_put: MDB_KEYEXIST: Key already exists
```

**原因:**
- 尝试插入已存在的键
- 使用了 MDB_NOOVERWRITE 标志

**解决方案:**

```c
// 方案 1: 更新现有值
int upsert(MDB_env *env, MDB_dbi dbi,
           const char *key, const void *data, size_t size) {
    MDB_txn *txn;
    MDB_val k, v;
    int rc;

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc != 0) return rc;

    k.mv_data = (void*)key;
    k.mv_size = strlen(key) + 1;
    v.mv_data = (void*)data;
    v.mv_size = size;

    // 不使用 MDB_NOOVERWRITE，允许更新
    rc = mdb_put(txn, dbi, &k, &v, 0);

    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    return mdb_txn_commit(txn);
}

// 方案 2: 检查键是否存在后再决定
int put_if_absent(MDB_env *env, MDB_dbi dbi,
                  const char *key, const void *data, size_t size) {
    MDB_txn *txn;
    MDB_val k, v;
    int rc;

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc != 0) return rc;

    k.mv_data = (void*)key;
    k.mv_size = strlen(key) + 1;

    // 检查是否存在
    rc = mdb_get(txn, dbi, &k, &v);
    if (rc == 0) {
        mdb_txn_abort(txn);
        return MDB_KEYEXIST;  // 键已存在
    }

    // 插入新值
    v.mv_data = (void*)data;
    v.mv_size = size;
    rc = mdb_put(txn, dbi, &k, &v, MDB_NOOVERWRITE);

    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    return mdb_txn_commit(txn);
}
```

### 4. MDB_CORRUPTED (-30796)

**错误信息:**
```
mdb_txn_begin: MDB_CORRUPTED: Database is corrupted
```

**原因:**
- 硬件故障
- 系统崩溃导致数据未正确写入
- 文件系统错误
- 直接修改数据库文件

**解决方案:**

```c
// 方案 1: 使用上一快照恢复
int open_previous_snapshot(MDB_env *env, const char *path) {
    int rc;

    // 关闭当前环境
    mdb_env_close(env);

    // 创建新环境
    rc = mdb_env_create(&env);
    if (rc != 0) return rc;

    // 使用 MDB_PREVSNAPSHOT 打开之前的快照
    rc = mdb_env_open(env, path, MDB_RDONLY | MDB_PREVSNAPSHOT, 0664);
    if (rc != 0) {
        mdb_env_close(env);
        return rc;
    }

    printf("Opened previous snapshot for recovery\n");
    return 0;
}

// 方案 2: 从备份恢复
int restore_from_backup(const char *db_path,
                       const char *backup_path) {
    // 使用 mdb_copy 创建备份
    // 假设已有备份文件

    // 删除损坏的数据库
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", db_path);
    system(cmd);

    // 恢复备份
    snprintf(cmd, sizeof(cmd), "cp -r %s %s", backup_path, db_path);
    system(cmd);

    printf("Database restored from backup\n");
    return 0;
}

// 方案 3: 尝试修复（有限能力）
// LMDB 不提供完整的修复工具
// 最好的方法是定期备份
```

**预防措施:**

```c
// 定期备份
int backup_database(MDB_env *env, const char *backup_path) {
    // 关闭环境以确保一致性
    mdb_env_close(env);

    // 使用 mdb_copy 工具
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "mdb_copy %s %s", db_path, backup_path);
    int rc = system(cmd);

    // 重新打开环境
    mdb_env_open(env, db_path, 0, 0664);

    return rc;
}
```

### 5. MDB_BAD_DBI (-30780)

**错误信息:**
```
mdb_put: MDB_BAD_DBI: Invalid database handle
```

**原因:**
- 使用了已关闭的数据库句柄
- 在不同事务间传递 DBI 句柄未正确处理
- 使用了未打开的数据库

**解决方案:**

```c
// 正确的数据库打开流程
int correct_dbi_usage(MDB_env *env, MDB_txn *txn, MDB_dbi *dbi) {
    int rc;

    // 在每个事务中打开数据库
    rc = mdb_dbi_open(txn, "my_database", MDB_CREATE, dbi);
    if (rc != 0) {
        fprintf(stderr, "Failed to open database: %s\n",
                mdb_strerror(rc));
        return rc;
    }

    // 使用数据库
    MDB_val key, data;
    // ... 操作 ...

    return 0;
}

// 使用全局 DBI（需要先打开）
static MDB_dbi global_dbi = 0;

int init_global_dbi(MDB_env *env) {
    MDB_txn *txn;
    int rc;

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc != 0) return rc;

    rc = mdb_dbi_open(txn, NULL, MDB_CREATE, &global_dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    mdb_txn_commit(txn);
    return 0;
}
```

### 6. MDB_TXN_FULL (-30788)

**错误信息:**
```
mdb_put: MDB_TXN_FULL: Transaction has too many dirty pages
```

**原因:**
- 单个事务修改了太多页面
- 超过了事务的脏页面限制

**解决方案:**

```c
// 方案 1: 分批处理
int batch_insert(MDB_env *env, MDB_dbi dbi,
                kv_pair *pairs, size_t count) {
    const size_t BATCH_SIZE = 1000;
    MDB_txn *txn;
    int rc;

    for (size_t i = 0; i < count; i += BATCH_SIZE) {
        rc = mdb_txn_begin(env, NULL, 0, &txn);
        if (rc != 0) return rc;

        size_t end = (i + BATCH_SIZE < count) ? i + BATCH_SIZE : count;
        for (size_t j = i; j < end; j++) {
            MDB_val key, data;
            key.mv_data = pairs[j].key;
            key.mv_size = pairs[j].key_size;
            data.mv_data = pairs[j].value;
            data.mv_size = pairs[j].value_size;

            rc = mdb_put(txn, dbi, &key, &data, 0);
            if (rc != 0) {
                mdb_txn_abort(txn);
                return rc;
            }
        }

        rc = mdb_txn_commit(txn);
        if (rc != 0) return rc;
    }

    return 0;
}

// 方案 2: 增加事务大小限制（有限帮助）
// 主要还是需要分批处理
```

---

## 性能问题诊断

### 问题 1: 写入速度慢

**诊断步骤:**

```c
// 性能分析代码
double benchmark_write(MDB_env *env, MDB_dbi dbi, int count) {
    struct timeval start, end;
    MDB_txn *txn;
    MDB_val key, data;
    int rc;

    gettimeofday(&start, NULL);

    for (int i = 0; i < count; i++) {
        rc = mdb_txn_begin(env, NULL, 0, &txn);
        if (rc != 0) {
            fprintf(stderr, "txn_begin failed at %d: %s\n",
                    i, mdb_strerror(rc));
            return -1;
        }

        char key_str[32], data_str[32];
        snprintf(key_str, sizeof(key_str), "key-%d", i);
        snprintf(data_str, sizeof(data_str), "value-%d", i);

        key.mv_data = key_str;
        key.mv_size = strlen(key_str) + 1;
        data.mv_data = data_str;
        data.mv_size = strlen(data_str) + 1;

        rc = mdb_put(txn, dbi, &key, &data, 0);
        if (rc != 0) {
            fprintf(stderr, "put failed at %d: %s\n",
                    i, mdb_strerror(rc));
            mdb_txn_abort(txn);
            return -1;
        }

        rc = mdb_txn_commit(txn);
        if (rc != 0) {
            fprintf(stderr, "commit failed at %d: %s\n",
                    i, mdb_strerror(rc));
            return -1;
        }
    }

    gettimeofday(&end, NULL);

    double elapsed = (end.tv_sec - start.tv_sec) +
                    (end.tv_usec - start.tv_usec) / 1000000.0;
    return count / elapsed;  // ops/sec
}

// 检查瓶颈
void diagnose_slow_write(MDB_env *env) {
    MDB_envinfo info;
    mdb_env_info(env, &info);

    printf("Map size: %.2f GB\n", info.me_mapsize / 1e9);
    printf("Last page: %zu\n", info.me_last_pgno);

    // 如果接近映射限制，写入会变慢
    if (info.me_last_pgno * 4096 > info.me_mapsize * 0.9) {
        printf("WARNING: Nearly out of map space!\n");
    }
}
```

**常见原因和解决:**

| 原因 | 解决方案 |
|-----|---------|
| 映射空间不足 | 增大 mapsize |
| 同步写入 | 使用 MDB_NOSYNC |
| 小事务过多 | 批量操作 |
| 键/值过大 | 优化数据结构 |
| 磁盘慢 | 升级到 SSD |

### 问题 2: 读取速度慢

**诊断代码:**

```c
double benchmark_read(MDB_env *env, MDB_dbi dbi, int count) {
    struct timeval start, end;
    MDB_txn *txn;
    MDB_val key, data;
    int rc;

    gettimeofday(&start, NULL);

    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) return -1;

    for (int i = 0; i < count; i++) {
        char key_str[32];
        snprintf(key_str, sizeof(key_str), "key-%d", i);

        key.mv_data = key_str;
        key.mv_size = strlen(key_str) + 1;

        rc = mdb_get(txn, dbi, &key, &data);
        if (rc != 0 && rc != MDB_NOTFOUND) {
            fprintf(stderr, "get failed at %d: %s\n",
                    i, mdb_strerror(rc));
        }
    }

    mdb_txn_abort(txn);

    gettimeofday(&end, NULL);

    double elapsed = (end.tv_sec - start.tv_sec) +
                    (end.tv_usec - start.tv_usec) / 1000000.0;
    return count / elapsed;
}
```

**优化建议:**

```c
// 预热缓存
void warm_cache(MDB_env *env, MDB_dbi dbi) {
    MDB_txn *txn;
    MDB_cursor *cursor;
    MDB_val key, data;

    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    mdb_cursor_open(txn, dbi, &cursor);

    // 顺序扫描预热缓存
    int rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    while (rc == 0) {
        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);

    printf("Cache warmed up\n");
}

// 使用游标批量读取
int batch_read(MDB_env *env, MDB_dbi dbi, int count) {
    MDB_txn *txn;
    MDB_cursor *cursor;
    MDB_val key, data;
    int rc;

    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    mdb_cursor_open(txn, dbi, &cursor);

    rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    int read_count = 0;
    while (rc == 0 && read_count < count) {
        // 处理数据
        process_data(data.mv_data, data.mv_size);

        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
        read_count++;
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);

    return read_count;
}
```

---

## 数据恢复

### 完整备份策略

```c
// 增量备份函数
int incremental_backup(const char *src_path,
                       const char *dest_path) {
    char cmd[512];

    // 使用 mdb_copy 进行原子备份
    snprintf(cmd, sizeof(cmd),
             "mdb_copy %s %s", src_path, dest_path);

    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "Backup failed: %d\n", rc);
        return -1;
    }

    printf("Backup completed: %s -> %s\n", src_path, dest_path);
    return 0;
}

// 定时备份守护进程
void* backup_daemon(void *arg) {
    const char *db_path = (const char*)arg;
    char backup_path[256];
    time_t now;

    while (1) {
        time(&now);
        struct tm *t = localtime(&now);

        // 每天备份一次
        snprintf(backup_path, sizeof(backup_path),
                "backup_%04d%02d%02d.mdb",
                t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);

        incremental_backup(db_path, backup_path);

        // 保留最近 7 天的备份
        // 删除旧备份...

        sleep(86400);  // 24 小时
    }

    return NULL;
}
```

### 数据导出和导入

```c
// 导出所有数据到文本格式
int export_database(MDB_env *env, MDB_dbi dbi,
                   const char *output_file) {
    FILE *fp = fopen(output_file, "w");
    if (!fp) {
        perror("fopen");
        return -1;
    }

    MDB_txn *txn;
    MDB_cursor *cursor;
    MDB_val key, data;
    int rc;

    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) {
        fclose(fp);
        return rc;
    }

    rc = mdb_cursor_open(txn, dbi, &cursor);
    if (rc != 0) {
        mdb_txn_abort(txn);
        fclose(fp);
        return rc;
    }

    rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    while (rc == 0) {
        // 写为 JSON 格式
        fprintf(fp, "{\"key\": \"");
        for (size_t i = 0; i < key.mv_size; i++) {
            fputc(((char*)key.mv_data)[i], fp);
        }
        fprintf(fp, "\", \"value\": \"");
        for (size_t i = 0; i < data.mv_size; i++) {
            fputc(((char*)data.mv_data)[i], fp);
        }
        fprintf(fp, "\"}\n");

        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    fclose(fp);

    printf("Exported to %s\n", output_file);
    return 0;
}
```

---

## 调试工具

### 数据库完整性检查

```c
// 遍历检查数据完整性
int verify_database(MDB_env *env, MDB_dbi dbi) {
    MDB_txn *txn;
    MDB_cursor *cursor;
    MDB_val key, data;
    int rc;
    int errors = 0;

    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) return rc;

    rc = mdb_cursor_open(txn, dbi, &cursor);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    // 统计
    size_t count = 0;
    size_t total_key_size = 0;
    size_t total_data_size = 0;

    rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    while (rc == 0) {
        count++;

        // 检查键和数据的有效性
        if (key.mv_size == 0 || key.mv_data == NULL) {
            fprintf(stderr, "ERROR: Invalid key at entry %zu\n", count);
            errors++;
        }

        if (data.mv_size == 0 || data.mv_data == NULL) {
            fprintf(stderr, "ERROR: Invalid data at entry %zu\n", count);
            errors++;
        }

        total_key_size += key.mv_size;
        total_data_size += data.mv_size;

        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);

    printf("Database verification:\n");
    printf("  Total entries: %zu\n", count);
    printf("  Total key size: %zu bytes\n", total_key_size);
    printf("  Total data size: %zu bytes\n", total_data_size);
    printf("  Average key size: %zu bytes\n",
           count > 0 ? total_key_size / count : 0);
    printf("  Average data size: %zu bytes\n",
           count > 0 ? total_data_size / count : 0);
    printf("  Errors found: %d\n", errors);

    return (errors == 0) ? 0 : -1;
}
```

### 事务日志分析

```c
// 显示事务统计
void show_transaction_stats(MDB_env *env) {
    MDB_envinfo info;
    MDB_stat stat;
    int rc;

    rc = mdb_env_info(env, &info);
    if (rc != 0) {
        fprintf(stderr, "Failed to get env info: %s\n",
                mdb_strerror(rc));
        return;
    }

    MDB_txn *txn;
    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) return;

    MDB_dbi dbi;
    rc = mdb_dbi_open(txn, NULL, 0, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return;
    }

    rc = mdb_stat(txn, dbi, &stat);
    if (rc == 0) {
        printf("Transaction Statistics:\n");
        printf("  Last txn ID: %zu\n", info.me_last_txnid);
        printf("  Active readers: %u / %u\n",
               info.me_numreaders, info.me_maxreaders);
        printf("  Database entries: %zu\n", stat.ms_entries);
        printf("  B-tree depth: %u\n", stat.ms_depth);
    }

    mdb_txn_abort(txn);
}
```

---

## 最佳实践

### 1. 错误处理

```c
// 始终检查返回值
int good_practice(MDB_env *env) {
    MDB_txn *txn;
    int rc;

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc != 0) {
        fprintf(stderr, "txn_begin failed: %s\n", mdb_strerror(rc));
        return rc;
    }

    // ... 执行操作 ...

    rc = mdb_txn_commit(txn);
    if (rc != 0) {
        fprintf(stderr, "txn_commit failed: %s\n", mdb_strerror(rc));
        return rc;
    }

    return 0;
}
```

### 2. 资源清理

```c
// 使用 goto 进行统一清理
int cleanup_example(MDB_env *env) {
    MDB_txn *txn = NULL;
    MDB_cursor *cursor = NULL;
    int rc = -1;

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc != 0) {
        fprintf(stderr, "Failed to begin transaction\n");
        goto cleanup;
    }

    MDB_dbi dbi;
    rc = mdb_dbi_open(txn, NULL, MDB_CREATE, &dbi);
    if (rc != 0) {
        fprintf(stderr, "Failed to open database\n");
        goto cleanup;
    }

    rc = mdb_cursor_open(txn, dbi, &cursor);
    if (rc != 0) {
        fprintf(stderr, "Failed to open cursor\n");
        goto cleanup;
    }

    // ... 使用游标 ...

    rc = 0;  // 成功

cleanup:
    if (cursor) mdb_cursor_close(cursor);
    if (txn) mdb_txn_abort(txn);
    return rc;
}
```

### 3. 并发安全

```c
// 正确的多线程访问
void* thread_func(void *arg) {
    thread_context_t *ctx = (thread_context_t*)arg;

    while (running) {
        // 每个线程创建自己的事务
        MDB_txn *txn;
        int rc = mdb_txn_begin(ctx->env, NULL, MDB_RDONLY, &txn);

        if (rc == 0) {
            // 执行读取
            MDB_val key, data;
            // ... 操作 ...

            mdb_txn_abort(txn);
        }

        usleep(1000);
    }

    return NULL;
}
```

### 4. 监控和维护

```c
// 定期健康检查
int health_check(MDB_env *env, MDB_dbi dbi) {
    MDB_envinfo info;
    MDB_stat stat;
    int rc;

    rc = mdb_env_info(env, &info);
    if (rc != 0) return -1;

    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &stat);
    if (rc != 0) return -1;

    rc = mdb_stat(stat, dbi, &stat);
    mdb_txn_abort(stat);

    if (rc != 0) return -1;

    // 检查各项指标
    int health_score = 100;

    // 检查空间使用
    size_t used_percent = (info.me_last_pgno * 100) /
                         (info.me_mapsize / 4096);
    if (used_percent > 90) {
        printf("WARNING: Database is %zu%% full\n", used_percent);
        health_score -= 20;
    }

    // 检查读者使用
    if (info.me_numreaders > info.me_maxreaders * 0.8) {
        printf("WARNING: Reader slots nearly full\n");
        health_score -= 10;
    }

    // 检查树深度
    if (stat.ms_depth > 50) {
        printf("WARNING: B-tree depth is %u (may impact performance)\n",
               stat.ms_depth);
        health_score -= 10;
    }

    printf("Health score: %d/100\n", health_score);
    return (health_score >= 70) ? 0 : -1;
}
```

---

## 总结

本故障排除指南涵盖了:

1. **常见错误**: MDB_MAP_FULL, MDB_READERS_FULL, MDB_KEYEXIST 等错误的解决方案
2. **性能问题**: 读写慢的诊断和优化方法
3. **数据恢复**: 备份策略和数据导出
4. **调试工具**: 完整性检查和统计分析
5. **最佳实践**: 错误处理、资源管理、并发安全

通过遵循本指南的建议，可以避免大多数常见问题并快速定位和解决故障。
