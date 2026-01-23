# LMDB 实战配方集 (LMDB Recipe Book)

本文档提供了 LMDB 常见使用模式的实用代码示例和最佳实践。

## 目录

1. [基础操作配方](#基础操作配方)
2. [性能优化配方](#性能优化配方)
3. [并发控制配方](#并发控制配方)
4. [数据处理配方](#数据处理配方)
5. [错误处理配方](#错误处理配方)
6. [高级模式配方](#高级模式配方)

---

## 基础操作配方

### 配方 1: 简单的键值存储

```c
// 设置值
int set_value(MDB_env *env, const char *key, const void *value, size_t size) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val k, v;
    int rc;

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc != 0) return rc;

    rc = mdb_dbi_open(txn, NULL, MDB_CREATE, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    k.mv_data = (void*)key;
    k.mv_size = strlen(key) + 1;
    v.mv_data = (void*)value;
    v.mv_size = size;

    rc = mdb_put(txn, dbi, &k, &v, 0);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    return mdb_txn_commit(txn);
}

// 获取值
int get_value(MDB_env *env, const char *key, void *value, size_t *size) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val k, v;
    int rc;

    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) return rc;

    rc = mdb_dbi_open(txn, NULL, 0, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    k.mv_data = (void*)key;
    k.mv_size = strlen(key) + 1;

    rc = mdb_get(txn, dbi, &k, &v);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    memcpy(value, v.mv_data, v.mv_size);
    if (size) *size = v.mv_size;

    mdb_txn_abort(txn);
    return 0;
}
```

### 配方 2: 批量插入优化

```c
// 批量插入（单次事务）
int batch_insert(MDB_env *env, kv_pair *pairs, size_t count) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val k, v;
    int rc;

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc != 0) return rc;

    rc = mdb_dbi_open(txn, NULL, MDB_CREATE, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    for (size_t i = 0; i < count; i++) {
        k.mv_data = pairs[i].key;
        k.mv_size = pairs[i].key_size;
        v.mv_data = pairs[i].value;
        v.mv_size = pairs[i].value_size;

        rc = mdb_put(txn, dbi, &k, &v, 0);
        if (rc != 0) {
            mdb_txn_abort(txn);
            return rc;
        }
    }

    return mdb_txn_commit(txn);
}
```

### 配方 3: 存在性检查

```c
// 检查键是否存在
int key_exists(MDB_env *env, MDB_dbi dbi, const char *key) {
    MDB_txn *txn;
    MDB_val k, v;
    int rc;

    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) return 0;

    k.mv_data = (void*)key;
    k.mv_size = strlen(key) + 1;

    rc = mdb_get(txn, dbi, &k, &v);
    mdb_txn_abort(txn);

    return rc == 0;
}

// 条件插入（仅当键不存在时）
int put_ifAbsent(MDB_env *env, MDB_dbi dbi,
                 const char *key, const void *value, size_t size) {
    MDB_txn *txn;
    MDB_val k, v;
    int rc;

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc != 0) return rc;

    k.mv_data = (void*)key;
    k.mv_size = strlen(key) + 1;

    // 检查是否已存在
    rc = mdb_get(txn, dbi, &k, &v);
    if (rc == 0) {
        mdb_txn_abort(txn);
        return MDB_KEYEXIST;  // 键已存在
    }

    // 插入新值
    v.mv_data = (void*)value;
    v.mv_size = size;

    rc = mdb_put(txn, dbi, &k, &v, MDB_NOOVERWRITE);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    return mdb_txn_commit(txn);
}
```

---

## 性能优化配方

### 配方 4: 环境配置优化

```c
// 优化的环境初始化
int init_optimized_env(MDB_env **env, const char *path,
                       size_t map_size, int max_readers) {
    int rc;

    rc = mdb_env_create(env);
    if (rc != 0) return rc;

    // 设置映射大小（建议至少 1GB）
    rc = mdb_env_set_mapsize(*env, map_size);
    if (rc != 0) goto error;

    // 设置最大数据库数（如果使用多个命名数据库）
    rc = mdb_env_set_maxdbs(*env, 16);
    if (rc != 0) goto error;

    // 设置最大读者数
    rc = mdb_env_set_maxreaders(*env, max_readers);
    if (rc != 0) goto error;

    // 打开环境（使用固定标志）
    rc = mdb_env_open(*env, path,
                      MDB_NOTLS | MDB_NOSYNC | MDB_NOMETASYNC, 0664);
    if (rc != 0) goto error;

    return 0;

error:
    mdb_env_close(*env);
    return rc;
}

// 环境配置建议：
// - map_size: 根据数据量设置，建议 1GB - 10GB
// - max_readers: 根据并发读者数设置，建议 >= 64
// - flags:
//   - MDB_NOTLS: 避免线程局部存储开销
//   - MDB_NOSYNC: 不立即同步到磁盘（更快速）
//   - MDB_NOMETASYNC: 跳过元数据同步
```

### 配方 5: 使用游标高效遍历

```c
// 使用游标遍历所有键值对
int iterate_all(MDB_env *env, MDB_dbi dbi,
                void (*callback)(const MDB_val*, const MDB_val*, void*),
                void *context) {
    MDB_txn *txn;
    MDB_cursor *cursor;
    MDB_val key, data;
    int rc;

    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) return rc;

    rc = mdb_cursor_open(txn, dbi, &cursor);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    // 从第一条记录开始
    rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    while (rc == 0) {
        callback(&key, &data, context);
        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);

    return (rc == MDB_NOTFOUND) ? 0 : rc;
}

// 范围查询
int range_query(MDB_env *env, MDB_dbi dbi,
                const char *start_key, const char *end_key,
                void (*callback)(const MDB_val*, const MDB_val*, void*),
                void *context) {
    MDB_txn *txn;
    MDB_cursor *cursor;
    MDB_val key, data;
    int rc;

    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) return rc;

    rc = mdb_cursor_open(txn, dbi, &cursor);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    // 定位到起始键
    key.mv_data = (void*)start_key;
    key.mv_size = strlen(start_key) + 1;

    rc = mdb_cursor_get(cursor, &key, &data, MDB_SET_RANGE);
    while (rc == 0) {
        // 检查是否超出范围
        if (end_key && strcmp(key.mv_data, end_key) > 0) {
            break;
        }

        callback(&key, &data, context);
        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);

    return (rc == MDB_NOTFOUND) ? 0 : rc;
}
```

### 配方 6: 零拷贝读取

```c
// 零拷贝读取（直接返回指针，无需 memcpy）
int zero_copy_get(MDB_txn *txn, MDB_dbi dbi,
                  const char *key, MDB_val *value) {
    MDB_val k;
    int rc;

    k.mv_data = (void*)key;
    k.mv_size = strlen(key) + 1;

    rc = mdb_get(txn, dbi, &k, value);
    if (rc != 0) return rc;

    // value->mv_data 现在指向数据库中的数据
    // 注意：事务结束后指针将失效！

    return 0;
}

// 使用示例
void zero_copy_example(MDB_env *env, MDB_dbi dbi, const char *key) {
    MDB_txn *txn;
    MDB_val value;
    int rc;

    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) return;

    rc = zero_copy_get(txn, dbi, key, &value);
    if (rc == 0) {
        // 直接访问数据，无需拷贝
        printf("Value: %.*s\n", (int)value.mv_size, (char*)value.mv_data);

        // 警告：不要在事务关闭后使用 value.mv_data！
    }

    mdb_txn_abort(txn);
}
```

---

## 并发控制配方

### 配方 7: 读写分离

```c
// 读者事务（可以并发）
MDB_txn* begin_read_transaction(MDB_env *env) {
    MDB_txn *txn;
    if (mdb_txn_begin(env, NULL, MDB_RDONLY, &txn) == 0) {
        return txn;
    }
    return NULL;
}

// 写者事务（独占访问）
MDB_txn* begin_write_transaction(MDB_env *env) {
    MDB_txn *txn;
    if (mdb_txn_begin(env, NULL, 0, &txn) == 0) {
        return txn;
    }
    return NULL;
}

// 使用示例
void* reader_thread(void *arg) {
    MDB_env *env = (MDB_env*)arg;

    while (running) {
        MDB_txn *txn = begin_read_transaction(env);
        if (txn) {
            // 执行读取操作
            MDB_val key, data;
            // ... 读取代码 ...

            mdb_txn_abort(txn);
        }
        usleep(10000);
    }
    return NULL;
}

void* writer_thread(void *arg) {
    MDB_env *env = (MDB_env*)arg;

    while (running) {
        MDB_txn *txn = begin_write_transaction(env);
        if (txn) {
            // 执行写入操作
            MDB_val key, data;
            // ... 写入代码 ...

            mdb_txn_commit(txn);
        }
        usleep(100000);
    }
    return NULL;
}
```

### 配方 8: 原子计数器

```c
// 原子递增操作
int atomic_increment(MDB_env *env, MDB_dbi dbi,
                     const char *key, int64_t delta,
                     int64_t *new_value) {
    MDB_txn *txn;
    MDB_val k, v;
    int rc;

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc != 0) return rc;

    k.mv_data = (void*)key;
    k.mv_size = strlen(key) + 1;

    // 读取当前值
    rc = mdb_get(txn, dbi, &k, &v);
    int64_t current = 0;

    if (rc == 0) {
        // 键存在，读取当前值
        if (v.mv_size == sizeof(int64_t)) {
            current = *(int64_t*)v.mv_data;
        }
    }

    // 递增
    *new_value = current + delta;

    // 写回
    v.mv_data = new_value;
    v.mv_size = sizeof(int64_t);

    rc = mdb_put(txn, dbi, &k, &v, 0);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    return mdb_txn_commit(txn);
}
```

### 配方 9: 条件更新 (Compare-And-Swap)

```c
// 比较并交换操作
int compare_and_swap(MDB_env *env, MDB_dbi dbi,
                     const char *key,
                     const void *expected, size_t expected_size,
                     const void *new_value, size_t new_size) {
    MDB_txn *txn;
    MDB_val k, v;
    int rc;

    rc = mdb_txn_begin(env, null, 0, &txn);
    if (rc != 0) return rc;

    k.mv_data = (void*)key;
    k.mv_size = strlen(key) + 1;

    // 读取当前值
    rc = mdb_get(txn, dbi, &k, &v);
    if (rc == MDB_NOTFOUND) {
        mdb_txn_abort(txn);
        return -1;  // 键不存在
    }
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    // 比较值
    if (v.mv_size != expected_size ||
        memcmp(v.mv_data, expected, expected_size) != 0) {
        mdb_txn_abort(txn);
        return 1;  // 值不匹配
    }

    // 更新值
    v.mv_data = (void*)new_value;
    v.mv_size = new_size;

    rc = mdb_put(txn, dbi, &k, &v, 0);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    return mdb_txn_commit(txn);
}
```

---

## 数据处理配方

### 配方 10: 去重集合

```c
// 添加元素到集合（自动去重）
int set_add(MDB_env *env, MDB_dbi dbi, const void *element, size_t size) {
    MDB_txn *txn;
    MDB_val key, data;
    int rc;

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc != 0) return rc;

    // 使用元素本身作为键
    key.mv_data = (void*)element;
    key.mv_size = size;

    // 使用空值（只需要键即可）
    data.mv_data = "";
    data.mv_size = 0;

    // MDB_NOOVERWRITE 确保不重复
    rc = mdb_put(txn, dbi, &key, &data, MDB_NOOVERWRITE);

    mdb_txn_commit(txn);

    return (rc == MDB_KEYEXIST) ? 1 : rc;
}

// 检查元素是否存在
int set_contains(MDB_env *env, MDB_dbi dbi,
                 const void *element, size_t size) {
    MDB_txn *txn;
    MDB_val key, data;
    int rc;

    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) return 0;

    key.mv_data = (void*)element;
    key.mv_size = size;

    rc = mdb_get(txn, dbi, &key, &data);
    mdb_txn_abort(txn);

    return rc == 0;
}
```

### 配方 11: 排序队列

```c
// 添加到优先级队列
int priority_queue_push(MDB_env *env, MDB_dbi dbi,
                        uint64_t priority, const void *data, size_t size) {
    MDB_txn *txn;
    MDB_val key, value;
    char key_str[32];
    int rc;

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc != 0) return rc;

    // 创建排序键（优先级反转以实现最大堆）
    snprintf(key_str, sizeof(key_str), "%020lu-%020lu",
             UINT64_MAX - priority, time(NULL) * 1000000 + rand() % 1000000);

    key.mv_data = key_str;
    key.mv_size = strlen(key_str) + 1;
    value.mv_data = (void*)data;
    value.mv_size = size;

    rc = mdb_put(txn, dbi, &key, &value, 0);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    return mdb_txn_commit(txn);
}

// 从优先级队列弹出
int priority_queue_pop(MDB_env *env, MDB_dbi dbi,
                       void *data, size_t *size) {
    MDB_txn *txn;
    MDB_cursor *cursor;
    MDB_val key, value;
    int rc;

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc != 0) return rc;

    rc = mdb_cursor_open(txn, dbi, &cursor);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    // 获取最高优先级项（第一条记录）
    rc = mdb_cursor_get(cursor, &key, &value, MDB_FIRST);
    if (rc != 0) {
        mdb_cursor_close(cursor);
        mdb_txn_abort(txn);
        return rc;
    }

    // 复制数据
    memcpy(data, value.mv_data, value.mv_size);
    if (size) *size = value.mv_size;

    // 删除该项
    mdb_cursor_del(cursor, 0);
    mdb_cursor_close(cursor);

    return mdb_txn_commit(txn);
}
```

### 配方 12: 时间窗口数据

```c
// 存储带时间戳的数据
int store_with_timestamp(MDB_env *env, MDB_dbi dbi,
                         const char *key, const void *data, size_t size) {
    MDB_txn *txn;
    MDB_val k, v;
    char time_key[64];
    int rc;

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc != 0) return rc;

    // 组合键：时间戳 + 原始键
    uint64_t timestamp = time(NULL);
    snprintf(time_key, sizeof(time_key), "%020lu-%s", timestamp, key);

    k.mv_data = time_key;
    k.mv_size = strlen(time_key) + 1;
    v.mv_data = (void*)data;
    v.mv_size = size;

    rc = mdb_put(txn, dbi, &k, &v, 0);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    return mdb_txn_commit(txn);
}

// 删除过期数据
int cleanup_old_data(MDB_env *env, MDB_dbi dbi, uint64_t age_seconds) {
    MDB_txn *txn;
    MDB_cursor *cursor;
    MDB_val key, data;
    char cutoff_key[32];
    int rc;
    int deleted = 0;

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc != 0) return rc;

    rc = mdb_cursor_open(txn, dbi, &cursor);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    // 计算截止时间戳
    uint64_t cutoff = time(NULL) - age_seconds;
    snprintf(cutoff_key, sizeof(cutoff_key), "%020lu", cutoff);

    // 找到第一条记录
    rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    while (rc == 0) {
        // 检查时间戳
        char *key_str = (char*)key.mv_data;
        uint64_t timestamp = atoll(key_str);

        if (timestamp >= cutoff) {
            break;  // 到达未过期数据
        }

        // 删除过期数据
        mdb_cursor_del(cursor, 0);
        deleted++;

        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    rc = mdb_txn_commit(txn);

    printf("Cleaned up %d old records\n", deleted);
    return rc;
}
```

---

## 错误处理配方

### 配方 13: 完整的错误处理

```c
// 完整的错误处理宏
#define CHECK_RC(rc, label) \
    do { \
        if (rc != 0) { \
            fprintf(stderr, "Error %s:%d: %s\n", \
                    __FILE__, __LINE__, mdb_strerror(rc)); \
            goto label; \
        } \
    } while(0)

// 使用示例
int safe_operation(MDB_env *env, const char *key, const void *data, size_t size) {
    MDB_txn *txn = NULL;
    MDB_dbi dbi;
    MDB_val k, v;
    int rc;

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    CHECK_RC(rc, cleanup);

    rc = mdb_dbi_open(txn, NULL, MDB_CREATE, &dbi);
    CHECK_RC(rc, abort_txn);

    k.mv_data = (void*)key;
    k.mv_size = strlen(key) + 1;
    v.mv_data = (void*)data;
    v.mv_size = size;

    rc = mdb_put(txn, dbi, &k, &v, 0);
    CHECK_RC(rc, abort_txn);

    rc = mdb_txn_commit(txn);
    CHECK_RC(rc, cleanup);

    return 0;

abort_txn:
    if (txn) mdb_txn_abort(txn);
cleanup:
    return rc;
}
```

### 配方 14: 环境恢复

```c
// 检查并恢复损坏的环境
int check_and_recover(MDB_env *env) {
    MDB_txn *txn;
    MDB_stat stat;
    int rc;

    // 尝试只读事务
    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) {
        fprintf(stderr, "Environment may be corrupted: %s\n", mdb_strerror(rc));

        // 尝试恢复（需要重新打开环境）
        mdb_env_close(env);
        rc = mdb_env_create(&env);
        if (rc != 0) return rc;

        rc = mdb_env_open(env, db_path, MDB_RDONLY | MDB_PREVSNAPSHOT, 0664);
        if (rc != 0) {
            fprintf(stderr, "Cannot open previous snapshot: %s\n", mdb_strerror(rc));
            return rc;
        }

        fprintf(stderr, "Opened previous snapshot for recovery\n");
        return 1;  // 指示使用了快照
    }

    mdb_txn_abort(txn);
    return 0;
}
```

### 配方 15: 事务重试逻辑

```c
// 带重试的事务操作
int transaction_with_retry(MDB_env *env,
                           int (*operation)(MDB_txn*, void*),
                           void *context, int max_retries) {
    MDB_txn *txn;
    int rc;
    int attempts = 0;

    while (attempts < max_retries) {
        attempts++;

        rc = mdb_txn_begin(env, NULL, 0, &txn);
        if (rc != 0) {
            if (rc == MDB_MAP_RESIZED) {
                // 映射大小已改变，重试
                continue;
            }
            return rc;
        }

        // 执行操作
        rc = operation(txn, context);
        if (rc != 0) {
            mdb_txn_abort(txn);
            if (rc == MDB_MAP_FULL && attempts < max_retries) {
                // 映射已满，重试
                usleep(1000);
                continue;
            }
            return rc;
        }

        // 提交事务
        rc = mdb_txn_commit(txn);
        if (rc != 0) {
            if (rc == MDB_MAP_FULL && attempts < max_retries) {
                // 映射已满，重试
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

## 高级模式配方

### 配方 16: 双数据库索引

```c
// 在两个数据库中维护索引
int indexed_insert(MDB_env *env,
                   MDB_dbi primary_db, MDB_dbi index_db,
                   const char *id_key,
                   const char *index_field,
                   const void *data, size_t size) {
    MDB_txn *txn;
    MDB_val key, value;
    char index_key[512];
    int rc;

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc != 0) return rc;

    // 插入主数据
    key.mv_data = (void*)id_key;
    key.mv_size = strlen(id_key) + 1;
    value.mv_data = (void*)data;
    value.mv_size = size;

    rc = mdb_put(txn, primary_db, &key, &value, 0);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    // 插入索引（索引字段 -> ID）
    snprintf(index_key, sizeof(index_key), "%s-%s", index_field, id_key);
    key.mv_data = index_key;
    key.mv_size = strlen(index_key) + 1;
    value.mv_data = (void*)id_key;
    value.mv_size = strlen(id_key) + 1;

    rc = mdb_put(txn, index_db, &key, &value, 0);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    return mdb_txn_commit(txn);
}

// 通过索引查询
int query_by_index(MDB_env *env,
                   MDB_dbi primary_db, MDB_dbi index_db,
                   const char *index_value,
                   void (*callback)(const MDB_val*, const MDB_val*)) {
    MDB_txn *txn;
    MDB_cursor *cursor;
    MDB_val key, data, primary_key, primary_data;
    char index_prefix[512];
    int rc;

    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) return rc;

    rc = mdb_cursor_open(txn, index_db, &cursor);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    // 设置索引前缀
    snprintf(index_prefix, sizeof(index_prefix), "%s-", index_value);
    key.mv_data = index_prefix;
    key.mv_size = strlen(index_prefix);

    // 查找匹配的索引项
    rc = mdb_cursor_get(cursor, &key, &data, MDB_SET_RANGE);
    while (rc == 0) {
        // 检查是否还在前缀范围内
        if (strncmp(key.mv_data, index_prefix, strlen(index_prefix)) != 0) {
            break;
        }

        // 使用索引中的 ID 获取主数据
        primary_key.mv_data = data.mv_data;
        primary_key.mv_size = data.mv_size;

        rc = mdb_get(txn, primary_db, &primary_key, &primary_data);
        if (rc == 0) {
            callback(&primary_key, &primary_data);
        }

        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);

    return (rc == MDB_NOTFOUND) ? 0 : rc;
}
```

### 配方 17: 数据库分片

```c
// 分片策略
typedef int (*shard_func)(const char *key, int num_shards);

// 简单的哈希分片
int hash_shard(const char *key, int num_shards) {
    unsigned int hash = 5381;
    int c;

    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c;  // hash * 33 + c
    }

    return hash % num_shards;
}

// 分片写入
int sharded_put(MDB_env **envs, int num_shards,
                shard_func shard_fn,
                const char *key, const void *data, size_t size) {
    int shard_idx = shard_fn(key, num_shards);
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val k, v;
    int rc;

    rc = mdb_txn_begin(envs[shard_idx], NULL, 0, &txn);
    if (rc != 0) return rc;

    rc = mdb_dbi_open(txn, NULL, MDB_CREATE, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    k.mv_data = (void*)key;
    k.mv_size = strlen(key) + 1;
    v.mv_data = (void*)data;
    v.mv_size = size;

    rc = mdb_put(txn, dbi, &k, &v, 0);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    return mdb_txn_commit(txn);
}

// 分片读取
int sharded_get(MDB_env **envs, int num_shards,
                shard_func shard_fn,
                const char *key, void *data, size_t *size) {
    int shard_idx = shard_fn(key, num_shards);
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val k, v;
    int rc;

    rc = mdb_txn_begin(envs[shard_idx], NULL, MDB_RDONLY, &txn);
    if (rc != 0) return rc;

    rc = mdb_dbi_open(txn, NULL, 0, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    k.mv_data = (void*)key;
    k.mv_size = strlen(key) + 1;

    rc = mdb_get(txn, dbi, &k, &v);
    if (rc == 0) {
        memcpy(data, v.mv_data, v.mv_size);
        if (size) *size = v.mv_size;
    }

    mdb_txn_abort(txn);
    return rc;
}
```

### 配方 18: 事务复制

```c
// 主从复制配置
typedef struct {
    MDB_env *master_env;
    MDB_env *replica_env;
    pthread_t replication_thread;
    int running;
} replication_ctx_t;

// 复制线程
void* replication_worker(void *arg) {
    replication_ctx_t *ctx = (replication_ctx_t*)arg;
    MDB_txn *master_txn, *replica_txn;
    MDB_cursor *master_cursor;
    MDB_val key, data;
    MDB_dbi master_dbi, replica_dbi;
    int rc;

    while (ctx->running) {
        // 开始主库读取事务
        rc = mdb_txn_begin(ctx->master_env, NULL, MDB_RDONLY, &master_txn);
        if (rc != 0) {
            usleep(1000000);
            continue;
        }

        rc = mdb_dbi_open(master_txn, NULL, 0, &master_dbi);
        if (rc != 0) {
            mdb_txn_abort(master_txn);
            usleep(1000000);
            continue;
        }

        // 开始副本写入事务
        rc = mdb_txn_begin(ctx->replica_env, NULL, 0, &replica_txn);
        if (rc != 0) {
            mdb_txn_abort(master_txn);
            usleep(1000000);
            continue;
        }

        rc = mdb_dbi_open(replica_txn, NULL, MDB_CREATE, &replica_dbi);
        if (rc != 0) {
            mdb_txn_abort(replica_txn);
            mdb_txn_abort(master_txn);
            usleep(1000000);
            continue;
        }

        // 遍历主库并复制到副本
        rc = mdb_cursor_open(master_txn, master_dbi, &master_cursor);
        if (rc != 0) {
            mdb_txn_abort(replica_txn);
            mdb_txn_abort(master_txn);
            usleep(1000000);
            continue;
        }

        rc = mdb_cursor_get(master_cursor, &key, &data, MDB_FIRST);
        while (rc == 0) {
            mdb_put(replica_txn, replica_dbi, &key, &data, 0);
            rc = mdb_cursor_get(master_cursor, &key, &data, MDB_NEXT);
        }

        mdb_cursor_close(master_cursor);

        // 提交副本事务
        mdb_txn_commit(replica_txn);
        mdb_txn_abort(master_txn);

        sleep(1);  // 每秒复制一次
    }

    return NULL;
}

// 启动复制
int start_replication(replication_ctx_t *ctx,
                      MDB_env *master, MDB_env *replica) {
    ctx->master_env = master;
    ctx->replica_env = replica;
    ctx->running = 1;

    return pthread_create(&ctx->replication_thread, NULL,
                         replication_worker, ctx);
}

// 停止复制
void stop_replication(replication_ctx_t *ctx) {
    ctx->running = 0;
    pthread_join(ctx->replication_thread, NULL);
}
```

---

## 总结

本配方集涵盖了 LMDB 的主要使用模式：

1. **基础操作**: 简单的键值存储、批量操作、存在性检查
2. **性能优化**: 环境配置、游标使用、零拷贝读取
3. **并发控制**: 读写分离、原子操作、条件更新
4. **数据处理**: 集合、队列、时间窗口数据
5. **错误处理**: 完整错误处理、环境恢复、事务重试
6. **高级模式**: 索引、分片、复制

这些配方可以直接应用于实际项目中，或作为构建更复杂系统的基础。
