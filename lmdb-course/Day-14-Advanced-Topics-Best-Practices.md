# LMDB 底层实现 14天课程 - Day 14

## 高级主题与最佳实践 - 成为 LMDB 专家

欢迎来到最后一天！经过13天的深入学习，你已经掌握了 LMDB 的核心实现。今天我们将探讨高级主题和最佳实践，帮助你成为真正的 LMDB 专家。

---

## 今天的目标

1. 掌握常见陷阱和注意事项
2. 学习性能调优技巧
3. 了解实际应用案例
4. 总结课程要点

---

## 14.1 常见陷阱

### 陷阱 1: 错误的事务使用

```c
// 错误：跨线程使用事务
// 线程 1
MDB_txn *txn;
mdb_txn_begin(env, NULL, 0, &txn);
// 传递 txn 到线程 2

// 线程 2
mdb_put(txn, dbi, &key, &data, 0);  // 错误！

// 正确：每个线程使用自己的事务
// 线程 1
MDB_txn *txn1;
mdb_txn_begin(env, NULL, 0, &txn1);
mdb_put(txn1, dbi, &key, &data, 0);
mdb_txn_commit(txn1);

// 线程 2
MDB_txn *txn2;
mdb_txn_begin(env, NULL, 0, &txn2);
mdb_put(txn2, dbi, &key, &data, 0);
mdb_txn_commit(txn2);
```

### 陷阱 2: 修改返回的数据

```c
// 错误：直接修改返回的数据
MDB_val data;
mdb_get(txn, dbi, &key, &data);
strcpy(data.mv_data, "modified");  // 危险！

// 问题：data.mv_data 指向映射内存
//      修改会损坏数据库

// 正确：先复制
char buf[256];
memcpy(buf, data.mv_data, data.mv_size);
strcpy(buf, "modified");  // 安全
```

### 陷阱 3: 忘记提交或中止

```c
// 错误：忘记提交
void insert_data(MDB_env *env) {
    MDB_txn *txn;
    mdb_txn_begin(env, NULL, 0, &txn);
    mdb_put(txn, dbi, &key, &data, 0);
    // 忘记提交！
    // 应该：mdb_txn_commit(txn);
}

// 正确：始终处理事务结果
int insert_data(MDB_env *env) {
    MDB_txn *txn;
    int rc;

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc) return rc;

    rc = mdb_put(txn, dbi, &key, &data, 0);
    if (rc) {
        mdb_txn_abort(txn);
        return rc;
    }

    return mdb_txn_commit(txn);
}
```

### 陷阱 4: 错误的映射大小

```c
// 错误：映射大小太小
mdb_env_create(&env);
mdb_env_set_mapsize(env, 1024 * 1024);  // 1MB
mdb_env_open(env, path, 0, 0664);

// 当数据超过 1MB 时会失败

// 正确：预留足够空间
mdb_env_set_mapsize(env, 1024 * 1024 * 1024);  // 1GB
// 或使用 MDB_FIXEDMAP 预分配
```

### 陷阱 5: 忽略错误码

```c
// 错误：忽略错误
mdb_put(txn, dbi, &key, &data, 0);  // 忽略返回值
mdb_cursor_get(cursor, &key, &data, MDB_NEXT);  // 忽略返回值

// 正确：检查所有错误
int rc = mdb_put(txn, dbi, &key, &data, 0);
if (rc != MDB_SUCCESS) {
    fprintf(stderr, "mdb_put failed: %s\n", mdb_strerror(rc));
    // 处理错误
}

rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
if (rc == MDB_NOTFOUND) {
    // 到达末尾
} else if (rc != MDB_SUCCESS) {
    // 其他错误
}
```

---

## 14.2 性能调优

### 调优 1: 选择合适的页面大小

```c
// LMDB 通常使用系统的页面大小
// 但可以影响性能

// 小页面 (4KB):
//   - 适合小键值
//   - 更好的页面填充率
//   - 更多的页面操作

// 大页面 (8KB, 16KB):
//   - 适合大键值
//   - 更少的页面操作
//   - 可能的内存浪费

// 查看：mdb_stat
```

### 调优 2: 合理的映射大小

```c
// 建议：
// 1. 预估数据大小
// 2. 乘以 2-3 倍
// 3. 考虑增长空间

// 示例：
mdb_env_set_mapsize(env, estimated_size * 3);

// 运行时动态调整：
mdb_env_set_mapsize(env, new_size);
```

### 调优 3: 批量操作

```c
// 差：逐条提交
for (int i = 0; i < 10000; i++) {
    mdb_txn_begin(env, NULL, 0, &txn);
    mdb_put(txn, dbi, &key, &data, 0);
    mdb_txn_commit(txn);  // 10000 次提交！
}

// 好：批量提交
mdb_txn_begin(env, NULL, 0, &txn);
for (int i = 0; i < 10000; i++) {
    mdb_put(txn, dbi, &key, &data, 0);
}
mdb_txn_commit(txn);  // 只提交一次
```

### 调优 4: 使用 MDB_NOSYNC 的权衡

```c
// 最高性能（可接受数据丢失）
mdb_env_open(env, path,
             MDB_WRITEMAP | MDB_NOSYNC | MDB_MAPASYNC,
             0664);

// 平衡性能和安全性
mdb_env_open(env, path,
             MDB_WRITEMAP | MDB_NOMETASYNC,
             0664);

// 最高安全性（默认）
mdb_env_open(env, path, 0, 0664);
```

### 调优 5: 优化键比较

```c
// 对于整数键，使用整数比较
mdb_set_compare(txn, dbi, mdb_cmp_int);

// 对于固定长度键，使用自定义比较
int custom_cmp(const MDB_val *a, const MDB_val *b) {
    // 快速路径：大小不同
    if (a->mv_size != b->mv_size)
        return a->mv_size - b->mv_size;

    // 快速路径：前几个字节
    uint32_t *ia = (uint32_t *)a->mv_data;
    uint32_t *ib = (uint32_t *)b->mv_data;
    if (*ia != *ib)
        return *ia - *ib;

    // 完整比较
    return memcmp(a->mv_data, b->mv_data, a->mv_size);
}

mdb_set_compare(txn, dbi, custom_cmp);
```

---

## 14.3 实际应用案例

### 案例 1: 键值存储缓存

```c
// 使用 LMDB 作为 Redis 风格的缓存

typedef struct {
    MDB_env *env;
    MDB_dbi dbi;
} Cache;

Cache *cache_open(const char *path) {
    Cache *cache = malloc(sizeof(Cache));

    mdb_env_create(&cache->env);
    mdb_env_set_mapsize(cache->env, 1024 * 1024 * 1024);
    mdb_env_set_maxdbs(cache->env, 1);
    mdb_env_open(cache->env, path, 0, 0664);

    return cache;
}

int cache_set(Cache *cache, const char *key,
              const void *value, size_t len, int ttl) {
    MDB_txn *txn;
    MDB_val k, v;

    mdb_txn_begin(cache->env, NULL, 0, &txn);

    k.mv_data = (void *)key;
    k.mv_size = strlen(key) + 1;

    // 值格式：[expire_time][data]
    char *buf = malloc(8 + len);
    uint64_t expire = time(NULL) + ttl;
    memcpy(buf, &expire, 8);
    memcpy(buf + 8, value, len);

    v.mv_data = buf;
    v.mv_size = 8 + len;

    int rc = mdb_put(txn, cache->dbi, &k, &v, 0);
    if (rc) {
        free(buf);
        mdb_txn_abort(txn);
        return rc;
    }

    mdb_txn_commit(txn);
    free(buf);
    return 0;
}
```

### 案例 2: 消息队列

```c
// 使用 LMDB 实现持久化消息队列

typedef struct {
    MDB_env *env;
    MDB_dbi dbi;
    uint64_t seq;
} Queue;

int queue_push(Queue *q, const void *data, size_t len) {
    MDB_txn *txn;
    MDB_val key, value;

    mdb_txn_begin(q->env, NULL, 0, &txn);

    // 键是序列号
    key.mv_data = &q->seq;
    key.mv_size = sizeof(q->seq);

    value.mv_data = (void *)data;
    value.mv_size = len;

    int rc = mdb_put(txn, q->dbi, &key, &value, MDB_APPEND);
    if (rc) {
        mdb_txn_abort(txn);
        return rc;
    }

    q->seq++;
    mdb_txn_commit(txn);
    return 0;
}

int queue_pop(Queue *q, void *data, size_t *len) {
    MDB_txn *txn;
    MDB_cursor *cursor;
    MDB_val key, value;

    mdb_txn_begin(q->env, NULL, 0, &txn);
    mdb_cursor_open(txn, q->dbi, &cursor);

    // 获取第一个元素
    int rc = mdb_cursor_get(cursor, &key, &value, MDB_FIRST);
    if (rc == MDB_NOTFOUND) {
        mdb_txn_abort(txn);
        return MDB_NOTFOUND;
    }

    // 复制数据
    if (data && value.mv_size <= *len) {
        memcpy(data, value.mv_data, value.mv_size);
        *len = value.mv_size;
    }

    // 删除
    mdb_cursor_del(cursor, 0);

    mdb_cursor_close(cursor);
    mdb_txn_commit(txn);
    return 0;
}
```

### 案例 3: 嵌入式索引

```c
// 为多个字段创建索引

typedef struct {
    MDB_env *env;
    MDB_dbi main_db;
    MDB_dbi name_idx;
    MDB_dbi email_idx;
} UserDB;

int user_insert(UserDB *db, User *user) {
    MDB_txn *txn;
    MDB_val key, data;

    mdb_txn_begin(db->env, NULL, 0, &txn);

    // 主记录：key = user_id
    key.mv_data = &user->id;
    key.mv_size = sizeof(user->id);
    data.mv_data = user;
    data.mv_size = sizeof(*user);
    mdb_put(txn, db->main_db, &key, &data, 0);

    // 名称索引：key = name, value = user_id
    key.mv_data = user->name;
    key.mv_size = strlen(user->name) + 1;
    data.mv_data = &user->id;
    data.mv_size = sizeof(user->id);
    mdb_put(txn, db->name_idx, &key, &data, 0);

    // 邮箱索引：key = email, value = user_id
    key.mv_data = user->email;
    key.mv_size = strlen(user->email) + 1;
    mdb_put(txn, db->email_idx, &key, &data, 0);

    mdb_txn_commit(txn);
    return 0;
}

User *user_find_by_name(UserDB *db, const char *name) {
    MDB_txn *txn;
    MDB_val key, data;
    uint64_t user_id;

    mdb_txn_begin(db->env, NULL, MDB_RDONLY, &txn);

    // 查找名称索引
    key.mv_data = (void *)name;
    key.mv_size = strlen(name) + 1;

    if (mdb_get(txn, db->name_idx, &key, &data) != 0) {
        mdb_txn_abort(txn);
        return NULL;
    }

    user_id = *(uint64_t *)data.mv_data;

    // 查找主记录
    key.mv_data = &user_id;
    key.mv_size = sizeof(user_id);

    if (mdb_get(txn, db->main_db, &key, &data) != 0) {
        mdb_txn_abort(txn);
        return NULL;
    }

    User *user = malloc(data.mv_size);
    memcpy(user, data.mv_data, data.mv_size);

    mdb_txn_abort(txn);
    return user;
}
```

---

## 14.4 调试技巧

### 使用 MDB_DEBUG

```c
// 编译时定义 MDB_DEBUG
gcc -DMDB_DEBUG=1 -lmdb your_program.c

// 启用详细日志输出
```

### 完整调试工具集

#### lmdb_debugger.c - LMDB 调试辅助工具

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lmdb.h>
#include <sys/stat.h>

// 打印环境状态
void print_env_status(MDB_env *env) {
    MDB_envinfo info;
    MDB_stat stat;
    MDB_txn *txn;

    printf("\n========== 环境状态 ==========\n");

    // 环境信息
    mdb_env_info(env, &info);
    printf("映射地址:      %p\n", info.me_mapaddr);
    printf("映射大小:      %zu MB\n", info.me_mapsize / (1024*1024));
    printf("最后页号:      %u\n", info.me_last_pgno);
    printf("最大读事务数:  %u\n", info.me_maxreaders);
    printf("当前读事务数:  %u\n", info.me_numreaders);

    // 统计信息
    if (mdb_txn_begin(env, NULL, MDB_RDONLY, &txn) == 0) {
        MDB_dbi dbi;
        if (mdb_dbi_open(txn, NULL, 0, &dbi) == 0) {
            mdb_stat(txn, dbi, &stat);
            printf("\n数据库统计:\n");
            printf("  页面大小:      %u bytes\n", stat.ms_psize);
            printf("  树的深度:      %u\n", stat.ms_depth);
            printf("  分支页数:      %zu\n", stat.ms_branch_pages);
            printf("  叶子页数:      %zu\n", stat.ms_leaf_pages);
            printf("  溢出页数:      %zu\n", stat.ms_overflow_pages);
            printf("  总条目数:      %zu\n", stat.ms_entries);

            mdb_dbi_close(env, dbi);
        }
        mdb_txn_abort(txn);
    }

    // 计算使用率
    size_t used_pages = info.me_last_pgno + 1;
    size_t free_pages = info.me_numfree_pages;
    double usage = (double)used_pages / (used_pages + free_pages) * 100.0;

    printf("\n空间使用:\n");
    printf("  已用页面:      %zu\n", used_pages);
    printf("  空闲页面:      %zu\n", free_pages);
    printf("  使用率:        %.2f%%\n", usage);
    printf("================================\n\n");
}

// 打印事务状态
void print_txn_status(MDB_txn *txn) {
    printf("\n========== 事务状态 ==========\n");
    printf("事务ID:        %llu\n", (unsigned long long)txn->mt_txnid);
    printf("标志:          0x%x\n", txn->mt_flags);

    if (txn->mt_flags & MDB_TXN_RDONLY) {
        printf("类型:          只读事务\n");
        if (txn->mt_u.reader) {
            printf("读者槽位:      %p\n", (void *)txn->mt_u.reader);
        }
    } else {
        printf("类型:          写事务\n");
        printf("下一页号:      %u\n", txn->mt_next_pgno);
    }

    printf("数据库数量:    %u\n", txn->mt_numdbs);
    printf("================================\n\n");
}

// 检查数据库健康状态
int check_database_health(MDB_env *env) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_stat stat;
    int health_score = 100;
    int issues = 0;

    printf("\n========== 健康检查 ==========\n");

    if (mdb_txn_begin(env, NULL, MDB_RDONLY, &txn) != 0) {
        printf("✗ 无法开始读事务\n");
        return 0;
    }

    if (mdb_dbi_open(txn, NULL, 0, &dbi) != 0) {
        printf("✗ 无法打开主数据库\n");
        mdb_txn_abort(txn);
        return 0;
    }

    mdb_stat(txn, dbi, &stat);

    // 检查 1: 树深度
    if (stat.ms_depth > 5) {
        printf("⚠ 树深度过深 (%u)，可能影响性能\n", stat.ms_depth);
        health_score -= 10;
        issues++;
    } else {
        printf("✓ 树深度正常 (%u)\n", stat.ms_depth);
    }

    // 检查 2: 溢出页比例
    size_t total_pages = stat.ms_branch_pages + stat.ms_leaf_pages;
    if (total_pages > 0 && stat.ms_overflow_pages > total_pages / 10) {
        printf("⚠ 溢出页过多 (%zu / %zu)\n",
               stat.ms_overflow_pages, total_pages);
        health_score -= 15;
        issues++;
    } else {
        printf("✓ 溢出页数量正常\n");
    }

    // 检查 3: 空数据库
    if (stat.ms_entries == 0) {
        printf("⚠ 数据库为空\n");
        health_score -= 5;
    } else {
        printf("✓ 数据库包含 %zu 条记录\n", stat.ms_entries);
    }

    mdb_txn_abort(txn);
    mdb_dbi_close(env, dbi);

    // 最终评分
    printf("\n健康评分: %d/100", health_score);
    if (health_score >= 80) {
        printf(" (优秀)\n");
    } else if (health_score >= 60) {
        printf(" (良好)\n");
    } else if (health_score >= 40) {
        printf(" (一般)\n");
    } else {
        printf(" (需要关注)\n");
    }

    if (issues == 0) {
        printf("✓ 未发现问题\n");
    } else {
        printf("✗ 发现 %d 个潜在问题\n", issues);
    }

    printf("================================\n\n");

    return health_score;
}

// 性能测试
void performance_test(MDB_env *env, int num_ops) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;
    struct timespec start, end;
    char key_buf[32], data_buf[32];

    printf("\n========== 性能测试 ==========\n");
    printf("操作数量: %d\n\n", num_ops);

    // 写入测试
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < num_ops; i++) {
        mdb_txn_begin(env, NULL, 0, &txn);
        mdb_dbi_open(txn, NULL, 0, &dbi);

        snprintf(key_buf, sizeof(key_buf), "key-%d", i);
        snprintf(data_buf, sizeof(data_buf), "value-%d", i);

        key.mv_data = key_buf;
        key.mv_size = strlen(key_buf);
        data.mv_data = data_buf;
        data.mv_size = strlen(data_buf);

        mdb_put(txn, dbi, &key, &data, 0);
        mdb_txn_commit(txn);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    double write_time = (end.tv_sec - start.tv_sec) +
                        (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("写入性能:\n");
    printf("  总耗时: %.3f 秒\n", write_time);
    printf("  吞吐量: %.0f ops/s\n", num_ops / write_time);

    // 读取测试
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < num_ops; i++) {
        mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
        mdb_dbi_open(txn, NULL, 0, &dbi);

        snprintf(key_buf, sizeof(key_buf), "key-%d", i);
        key.mv_data = key_buf;
        key.mv_size = strlen(key_buf);

        mdb_get(txn, dbi, &key, &data);

        mdb_txn_abort(txn);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    double read_time = (end.tv_sec - start.tv_sec) +
                       (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("\n读取性能:\n");
    printf("  总耗时: %.3f 秒\n", read_time);
    printf("  吞吐量: %.0f ops/s\n", num_ops / read_time);

    printf("================================\n\n");
}

int main(int argc, char **argv) {
    MDB_env *env;
    char *db_path = "./testdb";
    int rc;

    if (argc > 1) {
        db_path = argv[1];
    }

    // 打开环境
    rc = mdb_env_create(&env);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_create failed: %s\n", mdb_strerror(rc));
        return 1;
    }

    rc = mdb_env_open(env, db_path, 0, 0664);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_open failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    // 运行诊断
    print_env_status(env);
    check_database_health(env);
    performance_test(env, 1000);

    mdb_env_close(env);

    return 0;
}
```

编译运行：
```bash
gcc -o lmdb_debugger lmdb_debugger.c -llmdb -lrt
./lmdb_debugger /path/to/your/database
```

### 内存泄漏检测脚本

```bash
#!/bin/bash
# leak_check.sh - LMDB 内存泄漏检测脚本

echo "LMDB 内存泄漏检测"
echo "===================="

# 编译带调试信息的程序
gcc -g -O0 -o test_program your_program.c -llmdb

# 运行 valgrind
valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --verbose \
         --log-file=valgrind-out.txt \
         ./test_program

# 分析结果
echo ""
echo "泄漏检测结果："
grep "definitely lost" valgrind-out.txt
grep "indirectly lost" valgrind-out.txt
grep "possibly lost" valgrind-out.txt

if grep -q "0 bytes in 0 blocks" valgrind-out.txt; then
    echo "✓ 未检测到内存泄漏"
else
    echo "✗ 检测到内存泄漏，请查看 valgrind-out.txt"
fi
```

### 崩溃转储分析

```bash
#!/bin/bash
# crash_analyzer.sh - LMDB 崩溃分析脚本

DB_PATH=$1

if [ -z "$DB_PATH" ]; then
    echo "用法: $0 <数据库路径>"
    exit 1
fi

echo "LMDB 数据库分析"
echo "================"

# 检查数据文件
if [ ! -f "$DB_PATH/data.mdb" ]; then
    echo "✗ 数据文件不存在"
    exit 1
fi

# 检查锁文件
if [ -f "$DB_PATH/lock.mdb" ]; then
    echo "✓ 锁文件存在"
    ls -lh "$DB_PATH/lock.mdb"

    # 检查锁文件大小
    LOCK_SIZE=$(stat -f%z "$DB_PATH/lock.mdb" 2>/dev/null || stat -c%s "$DB_PATH/lock.mdb")
    EXPECTED_SIZE=112

    if [ "$LOCK_SIZE" -eq "$EXPECTED_SIZE" ]; then
        echo "✓ 锁文件大小正常"
    else
        echo "⚠ 锁文件大小异常 (期望: $EXPECTED_SIZE, 实际: $LOCK_SIZE)"
    fi
else
    echo "✗ 锁文件缺失"
fi

# 使用 mdb_stat 检查
if command -v mdb_stat &> /dev/null; then
    echo ""
    echo "数据库统计:"
    mdb_stat "$DB_PATH"
fi

echo ""
echo "分析完成"
```

---

## 14.5 课程总结

### 我们学到了什么？

```
Day 1:  LMDB 概述与架构
Day 2:  内存映射 I/O 基础
Day 3:  数据库环境 (MDB_env)
Day 4:  页面结构与布局
Day 5:  B+树实现
Day 6:  事务管理 (上)
Day 7:  事务管理 (下)
Day 8:  MVCC 与版本管理
Day 9:  游标实现
Day 10: 锁管理
Day 11: 写操作与写时复制
Day 12: 空闲列表与空间管理
Day 13: 平台特定优化
Day 14: 高级主题与最佳实践
```

### 核心概念回顾

| 概念 | 关键点 |
|------|--------|
| **mmap** | 零拷贝，操作系统管理缓存 |
| **B+树** | O(log n) 查找，自平衡 |
| **MVCC** | 读写不阻塞，多版本共存 |
| **COW** | 写时复制，保证原子性 |
| **事务** | ACID 保证，单写入者 |
| **页面** | 基本单位，4KB/8KB |
| **游标** | 遍历工具，维护页面栈 |

---

## 14.6 进一步学习

### 推荐资源

```
源代码：
  - mdb.c (11,000 行核心实现)
  - lmdb.h (完整 API 文档)
  - 测试程序 (mtest*.c)

文档：
  - LMDB 官方文档
  - OpenLDAP 文档
  - 源代码注释

实践：
  - 阅读 mdb.c 源码
  - 使用 mtest 学习 API
  - 构建自己的应用
```

### 进阶主题

```
1. MDB_VL32 模式（32 位地址空间）
2. 自定义比较函数
3. 嵌入式数据库设计
4. 分布式数据库集成
5. 性能基准测试
6. 故障恢复机制
```

---

## 14.7 结语

经过 14 天的深入学习，你已经：

1. ✅ 理解了 LMDB 的核心架构
2. ✅ 掌握了底层实现机制
3. ✅ 学会了性能优化技巧
4. ✅ 了解了最佳实践

**你现在是 LMDB 专家了！**

继续探索源代码，实践所学知识，构建高性能的应用。

---

## 课程文件索引

```
lmdb-course/
├── Day-01-LMDB-Overview.md
├── Day-02-Memory-Mapped-IO.md
├── Day-03-Database-Environment.md
├── Day-04-Page-Structure.md
├── Day-05-BPlus-Tree.md
├── Day-06-Transaction-Management-Part1.md
├── Day-07-Transaction-Management-Part2.md
├── Day-08-MVCC-Version-Management.md
├── Day-09-Cursor-Implementation.md
├── Day-10-Lock-Management.md
├── Day-11-Write-Operations-Copy-on-Write.md
├── Day-12-Free-List-Space-Management.md
├── Day-13-Platform-Specific-Optimizations.md
└── Day-14-Advanced-Topics-Best-Practices.md
```

---

## 练习项目

1. **实现一个简单的 KV 存储**
   - 支持 GET/PUT/DELETE
   - 持久化到磁盘
   - 基本错误处理

2. **实现一个计数器**
   - 原子递增
   - 分布式友好
   - 持久化

3. **实现一个全文索引**
   - 倒排索引
   - 前缀搜索
   - 模糊匹配

4. **性能基准测试**
   - 对比不同的键大小
   - 对比不同的值大小
   - 测试并发性能

---

## 14.8 综合常见问题解答

### Q1: LMDB 和 Redis 相比有什么优缺点？

**A:** 对比分析：

```
LMDB 优势：
1. 持久化存储（磁盘）
2. 无需额外配置
3. 更小的内存占用
4. ACID 事务保证
5. 单文件，易于备份

Redis 优势：
1. 丰富的数据结构
2. 网络访问支持
3. 主从复制
4. 集群支持
5. 更高的写入吞吐量（纯内存）

选择建议：
- 需要持久化 -> LMDB
- 需要远程访问 -> Redis
- 嵌入式应用 -> LMDB
- 分布式缓存 -> Redis
```

### Q2: 什么时候应该使用多个数据库（DBI）？

**A:** 多数据库使用场景：

```c
// 适用场景：
1. 数据隔离
   - 用户数据 vs 系统数据
   - 不同模块的数据

2. 不同的访问模式
   - 高频读取 vs 低频访问
   - 需要不同的比较函数

3. 数据生命周期
   - 临时数据 vs 持久数据
   - 可以独立删除

// 注意：
- 最多支持 MDB_MAX_DBI (默认 256)
- 每个数据库有独立的元数据
- 开启新数据库需要设置 maxdbs
```

### Q3: 如何监控 LMDB 的性能？

**A:** 监控方法：

```c
// 1. 使用 mdb_stat
MDB_stat stat;
mdb_stat(txn, dbi, &stat);
printf("Depth: %u, Entries: %zu\n", stat.ms_depth, stat.ms_entries);

// 2. 监控事务耗时
struct timeval start, end;
gettimeofday(&start, NULL);
mdb_txn_begin(env, NULL, 0, &txn);
// ... 操作 ...
mdb_txn_commit(txn);
gettimeofday(&end, NULL);
long elapsed = (end.tv_sec - start.tv_sec) * 1000000 +
               (end.tv_usec - start.tv_usec);

// 3. 监控空闲页面
MDB_envinfo info;
mdb_env_info(env, &info);
printf("Free pages: %zu\n", info.me_numfree_pages);

// 4. 自定义性能计数器
typedef struct {
    size_t num_reads;
    size_t num_writes;
    size_t total_read_time;
    size_t total_write_time;
} PerfCounters;
```

### Q4: LMDB 如何处理数据库损坏？

**A:** 损坏处理：

```
损坏类型：
1. 元数据页损坏
   - 使用另一个元数据页
   - 两个都损坏 -> 无法恢复

2. 数据页损坏
   - 校验和检测（可选编译）
   - 跳过损坏页面

3. 崩溃恢复
   - 自动选择有效的元数据页
   - 未提交的数据自动丢弃

预防措施：
1. 定期备份
2. 使用 MDB_NOSYNC 时注意风险
3. 监控错误日志
4. 使用健壮的硬件
```

### Q5: 如何在多进程环境中安全使用 LMDB？

**A:** 多进程最佳实践：

```c
// 1. 共享环境
// 所有进程打开同一个数据库路径

// 进程 A (写入者)
mdb_env_create(&env);
mdb_env_open(env, "/path/to/db", 0, 0664);
mdb_txn_begin(env, NULL, 0, &txn);
// 写入操作
mdb_txn_commit(txn);

// 进程 B (读取者)
mdb_env_create(&env);
mdb_env_open(env, "/path/to/db", 0, 0664);
mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
// 读取操作
mdb_txn_abort(txn);

// 注意事项：
1. 同时只有一个写事务
2. 设置合适的 maxreaders
3. 使用文件锁协调
4. 避免使用 MDB_NOLOCK
```

### Q6: LMDB 的最佳键大小是多少？

**A:** 键大小建议：

```
性能最优：8-32 字节
- 适合缓存行
- 减少内存复制
- 更好的页面填充率

实际考虑：
1. 小于 8 字节
   - 使用固定大小键优化 (MDB_INTEGERKEY)

2. 8-64 字节
   - 性能平衡点

3. 64-256 字节
   - 可接受但影响性能

4. 大于 256 字节
   - 考虑使用键哈希
   - 或将键的一部分存储在值中

示例：
// 差：整个 URL 作为键
"https://example.com/path/to/resource" (42 bytes)

// 好：使用资源 ID
"resource:12345" (14 bytes)
```

### Q7: 如何实现数据库的备份和恢复？

**A:** 备份恢复方法：

```c
// 方法 1: 使用 mdb_copy（推荐）
// 离线备份，保证一致性
system("mdb_copy /path/to/source /path/to/backup");

// 方法 2: 在线备份（使用 MDB_CP_COMPACT）
MDB_env *env;
mdb_env_create(&env);
mdb_env_open(env, "/path/to/source", 0, 0664);
mdb_env_copy2(env, "/path/to/backup", MDB_CP_COMPACT);
mdb_env_close(env);

// 方法 3: 快照备份（Linux）
// 1. 创建文件系统快照
// 2. 从快照复制
// 3. 删除快照

// 恢复：
// 直接复制备份文件到数据目录
```

---

**恭喜你完成课程！祝你在 LMDB 的探索之旅中一切顺利！**

---
**最终参考文献：**
- lmdb.h - 完整 API 文档
- mdb.c - 完整实现（11,000 行）
- 测试程序和工具
- OpenLDAP 文档
