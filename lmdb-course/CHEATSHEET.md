# LMDB 快速参考卡片

## API 快速参考

### 环境管理
```c
// 创建环境
MDB_env *env;
mdb_env_create(&env);
mdb_env_set_mapsize(env, size);
mdb_env_set_maxdbs(env, count);
mdb_env_open(env, path, flags, mode);

// 关闭环境
mdb_env_close(env);
```

### 事务操作
```c
// 开始事务
MDB_txn *txn;
mdb_txn_begin(env, parent, flags, &txn);

// 提交事务
mdb_txn_commit(txn);

// 中止事务
mdb_txn_abort(txn);
```

### 数据库操作
```c
// 打开数据库
MDB_dbi dbi;
mdb_dbi_open(txn, name, flags, &dbi);

// 关闭数据库
mdb_dbi_close(env, dbi);
```

### 键值操作
```c
// 插入/更新
mdb_put(txn, dbi, &key, &data, flags);

// 查询
mdb_get(txn, dbi, &key, &data);

// 删除
mdb_del(txn, dbi, &key, &data);
```

### 游标操作
```c
// 创建游标
MDB_cursor *cursor;
mdb_cursor_open(txn, dbi, &cursor);

// 移动游标
mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
mdb_cursor_get(cursor, &key, &data, MDB_LAST);
mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
mdb_cursor_get(cursor, &key, &data, MDB_PREV);
mdb_cursor_get(cursor, &key, &data, MDB_SET);
mdb_cursor_get(cursor, &key, &data, MDB_SET_RANGE);

// 关闭游标
mdb_cursor_close(cursor);
```

---

## 标志位参考

### 环境标志 (mdb_env_open)
```c
MDB_FIXEDMAP     // 固定映射地址
MDB_NOSUBDIR     // 不使用子目录
MDB_NOSYNC       // 不同步数据
MDB_RDONLY       // 只读模式
MDB_WRITEMAP     // 写映射模式
MDB_MAPASYNC     // 异步刷新
MDB_NOTLS        // 禁用线程本地存储
MDB_NOLOCK       // 无锁模式
MDB_NOMETASYNC   // 不同步元数据
```

### 事务标志 (mdb_txn_begin)
```c
MDB_RDONLY       // 只读事务
```

### 数据库标志 (mdb_dbi_open)
```c
MDB_REVERSEKEY   // 反向键比较
MDB_DUPSORT      // 排序重复数据
MDB_INTEGERKEY    // 整数键
MDB_DUPFIXED     // 固定大小重复数据
MDB_INTEGERDUP    // 整数重复数据
MDB_REVERSEDUP    // 反向重复数据比较
MDB_CREATE        // 创建数据库（如果不存在）
```

### 游标操作标志 (mdb_cursor_get)
```c
MDB_FIRST         // 第一个条目
MDB_LAST          // 最后一个条目
MDB_NEXT          // 下一个条目
MDB_PREV          // 上一个条目
MDB_CURRENT       // 当前条目
MDB_SET           // 设置键
MDB_SET_RANGE     // 设置键范围
MDB_SET_KEY       // 设置键（仅用于 DUPSORT）
MDB_NEXT_DUP      // 下一个重复数据
MDB_PREV_DUP      // 上一个重复数据
```

### Put 操作标志 (mdb_put)
```c
MDB_NODUPDATA     // 不允许重复数据
MDB_NOOVERWRITE   // 不覆盖已存在的值
MDB_RESERVE       // 预留空间
MDB_APPEND        // 追加模式
```

---

## 错误码参考

### 常见错误码
```c
MDB_SUCCESS           // 成功 (0)
MDB_KEYEXIST          // 键已存在 (-30799)
MDB_NOTFOUND          // 键未找到 (-30798)
MDB_PAGE_NOTFOUND     // 页面未找到 (-30797)
MDB_CORRUPTED         // 数据库损坏 (-30796)
MDB_PANIC             // 致命错误 (-30795)
MDB_VERSION_MISMATCH   // 版本不匹配 (-30794)
MDB_INVALID           // 无效参数 (-30793)
MDB_MAP_FULL          // 映射空间满 (-30792)
MDB_DBS_FULL          // 数据库满 (-30791)
MDB_READERS_FULL      // 读者表满 (-30790)
MDB_TLS_FULL          // TLS 满 (-30789)
MDB_TXN_FULL          // 事务满 (-30788)
MDB_CURSOR_FULL       // 游标满 (-30787)
MDB_PAGE_FULL         // 页面满 (-30786)
MDB_MAP_RESIZED       // 映射已调整大小 (-30785)
MDB_INCOMPATIBLE      // 不兼容 (-30784)
MDB_BAD_RSLOT         // 错误的读者槽位 (-30783)
MDB_BAD_TXN           // 错误的事务 (-30782)
MDB_BAD_VALSIZE       // 错误的值大小 (-30781)
MDB_BAD_DBI           // 错误的数据库 (-30780)
MDB_PROBLEM           // 问题 (-30779)
MDB_LAST_ERRCODE      // 最后的错误码
```

---

## 数据结构速查

### MDB_val - 数据值
```c
typedef struct MDB_val {
    size_t  mv_size;  // 大小
    void   *mv_data;  // 数据指针
} MDB_val;
```

### MDB_stat - 统计信息
```c
typedef struct MDB_stat {
    unsigned int ms_psize;          // 页面大小
    unsigned int ms_depth;          // 树的深度
    size_t      ms_branch_pages;   // 分支页数量
    size_t      ms_leaf_pages;     // 叶子页数量
    size_t      ms_overflow_pages; // 溢出页数量
    size_t      ms_entries;        // 条目数量
} MDB_stat;
```

### MDB_envinfo - 环境信息
```c
typedef struct MDB_envinfo {
    void    *me_mapaddr;      // 映射地址
    size_t   me_mapsize;      // 映射大小
    size_t   me_last_pgno;    // 最后页号
    size_t   me_numreaders;   // 读事务数
    size_t   me_maxreaders;   // 最大读事务数
} MDB_envinfo;
```

---

## 性能优化提示

### 1. 批量操作
```c
// 好的做法
mdb_txn_begin(env, NULL, 0, &txn);
for (int i = 0; i < N; i++) {
    mdb_put(txn, dbi, &key, &data, 0);
}
mdb_txn_commit(txn);

// 差的做法
for (int i = 0; i < N; i++) {
    mdb_txn_begin(env, NULL, 0, &txn);
    mdb_put(txn, dbi, &key, &data, 0);
    mdb_txn_commit(txn);
}
```

### 2. 选择合适的键大小
- 最佳：8-32 字节
- 避免超过 256 字节
- 使用整数键优化 (MDB_INTEGERKEY)

### 3. 合理设置映射大小
```c
// 小应用
mdb_env_set_mapsize(env, 1024 * 1024 * 100);  // 100MB

// 大应用
mdb_env_set_mapsize(env, 1024ULL * 1024 * 1024 * 10);  // 10GB
```

### 4. 使用游标批量读取
```c
MDB_cursor *cursor;
mdb_cursor_open(txn, dbi, &cursor);

mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
while (rc == 0) {
    // 处理数据
    rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
}

mdb_cursor_close(cursor);
```

---

## 调试技巧

### GDB 命令
```bash
# 设置断点
(gdb) break mdb_txn_begin
(gdb) break mdb_txn_commit

# 查看变量
(gdb) print *env
(gdb) print *txn
(gdb) print txn->mt_txnid
(gdb) print txn->mt_flags

# 单步执行
(gdb) step
(gdb) next
(gdb) continue
```

### 编译调试版本
```bash
# 启用调试宏
gcc -DMDB_DEBUG=1 -g -O0 program.c -llmdb

# 内存检查
valgrind --leak-check=full ./program
```

---

## 常见问题解决

### MDB_MAP_FULL
```c
// 解决方案：增加映射大小
mdb_env_set_mapsize(env, new_larger_size);
```

### MDB_READERS_FULL
```c
// 解决方案：增加最大读事务数
mdb_env_set_maxreaders(env, 256);
```

### MDB_TXN_FULL
```c
// 原因：已有活跃的写事务
// 解决方案：等待现有写事务完成
```

### 崩溃恢复
```bash
# LMDB 自动恢复
# 如果失败，尝试：
mdb_copy original_db backup_db
mdb_copy -c original_db recovered_db
```

---

## 实用命令

### 编译
```bash
cd libraries/liblmdb
make
```

### 测试
```bash
make test
./mtest
./mtest2
./mtest3
./mtest4
./mtest5
```

### 工具使用
```bash
# 数据库统计
mdb_stat /path/to/db

# 数据库复制
mdb_copy /path/to/src /path/to/dest

# 数据库转储
mdb_dump -f output.txt /path/to/db

# 数据库加载
mdb_load -f input.txt /path/to/db

# 数据库删除
mdb_drop -f /path/to/db
```

---

## 最佳实践清单

### ✅ DO（推荐做法）
- 批量操作放在单个事务中
- 设置合适的映射大小
- 使用游标进行范围查询
- 检查所有返回值
- 及时关闭游标
- 使用 MDB_RDONLY 标志进行读操作

### ❌ DON'T（避免做法）
- 不要跨线程使用事务
- 不要忘记提交或中止事务
- 不要修改 mdb_get 返回的数据
- 不要使用过小的映射大小
- 不要忽略错误码
- 不要在循环中频繁提交小事务

---

## 代码模板

### 基本读写模板
```c
#include <lmdb.h>
#include <stdio.h>

int main() {
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;
    int rc;

    // 创建环境
    rc = mdb_env_create(&env);
    rc = mdb_env_set_mapsize(env, 1024 * 1024 * 100);
    rc = mdb_env_open(env, "./testdb", 0, 0664);

    // 写入
    rc = mdb_txn_begin(env, NULL, 0, &txn);
    rc = mdb_dbi_open(txn, NULL, 0, &dbi);
    key = (MDB_val){"key", 3};
    data = (MDB_val){"value", 5};
    rc = mdb_put(txn, dbi, &key, &data, 0);
    rc = mdb_txn_commit(txn);

    // 读取
    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    rc = mdb_dbi_open(txn, NULL, 0, &dbi);
    rc = mdb_get(txn, dbi, &key, &data);
    if (rc == 0) {
        printf("Value: %.*s\n", (int)data.mv_size, (char *)data.mv_data);
    }
    mdb_txn_abort(txn);

    // 清理
    mdb_dbi_close(env, dbi);
    mdb_env_close(env);

    return 0;
}
```

---

**提示：** 打印此文件作为快速参考！
