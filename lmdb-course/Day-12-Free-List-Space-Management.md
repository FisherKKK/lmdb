# LMDB 底层实现 14天课程 - Day 12

## 空闲列表与空间管理 - 高效利用存储

欢迎回来！今天我们将深入 LMDB 的空间管理机制。理解空闲列表的工作原理对于掌握 LMDB 的存储效率至关重要。

---

## 今天的目标

1. 理解空闲列表的结构
2. 掌握页面回收机制
3. 学习空间分配策略
4. 理解 Free DB 的组织

---

## 12.1 空闲列表概述

### 为什么需要空闲列表？

```
问题：删除数据后，页面如何重用？

解决方案：空闲列表
  • 跟踪所有可用的页面
  • 新数据优先使用空闲页
  • 避免文件无限增长
```

### LMDB 的空闲列表层次

```
三层空闲列表结构：

1. 事务本地空闲列表 (mt_free_pgs)
   • 当前事务释放的页面
   • 只在当前事务内可见
   • 优先级最高

2. 环境 Free DB (FREE_DBI = 0)
   • 全局空闲页面
   • 持久化存储
   • 跨事务共享

3. 扩展文件
   • 从文件末尾分配
   • 优先级最低
```

---

## 12.2 Free DB 结构

### MDB_db 中的空闲信息

```c
// Free DB 是一个特殊的数据库 (dbi = 0)
// 元数据页中的 mm_dbs[0] 就是 Free DB

typedef struct MDB_db {
    uint32_t  md_pad;         // 页大小
    uint16_t  md_flags;       // 标志
    uint16_t  md_depth;       // 树深度
    pgno_t    md_branch_pages; // 分支页数
    pgno_t    md_leaf_pages;   // 叶子页数
    pgno_t    md_overflow_pages; // 溢出页数
    mdb_size_t md_entries;     // 条目数（空闲页数）
    pgno_t    md_root;         // 根页号
} MDB_db;

// Free DB 的组织：
// 键:   空闲页的起始事务ID
// 数据: 页号列表 (IDL - ID List)
```

### IDL (ID List) 格式

```c
// LMDB 使用特殊的压缩格式存储页号列表
// IDL 是一个排序的、唯一的页号数组

typedef mdb_size_t *MDB_IDL;

// IDL 结构:
// [0] = 列表长度
// [1] = 最小页号
// [2] = 第二小页号
// ...
// [n] = 最大页号

// 示例：页面 [5, 10, 15, 20] 的 IDL
// idl[0] = 4
// idl[1] = 5
// idl[2] = 10
// idl[3] = 15
// idl[4] = 20
```

---

## 12.3 页面回收

### 删除页面时的回收

```c
// 当页面被删除或替换时
static int mdb_page_free(MDB_txn *txn, MDB_page *mp)
{
    pgno_t pgno = MP_PGNO(mp);
    int rc;

    // 1. 检查是否已经释放
    //    避免重复释放

    // 2. 添加到事务本地空闲列表
    rc = mdb_midl_append(&txn->mt_free_pgs, pgno);
    if (rc)
        return rc;

    // 3. 如果有父事务，也添加到父事务
    if (txn->mt_parent) {
        rc = mdb_midl_append(&txn->mt_parent->mt_free_pgs, pgno);
        if (rc)
            return rc;
    }

    return MDB_SUCCESS;
}
```

### 提交时合并到 Free DB

```c
// 事务提交时，将本地空闲列表合并到 Free DB
static int mdb_freelist_save(MDB_txn *txn)
{
    MDB_cursor *mc;
    MDB_val key, data;
    int rc;

    // 1. 打开 Free DB 的游标
    rc = mdb_cursor_open(txn, FREE_DBI, &mc);
    if (rc)
        return rc;

    // 2. 遍历本地空闲列表
    if (txn->mt_free_pgs[0] > 0) {
        // 按事务ID分组
        key.mv_size = sizeof(txnid_t);
        key.mv_data = &txn->mt_txnid;

        data.mv_size = txn->mt_free_pgs[0] * sizeof(pgno_t);
        data.mv_data = txn->mt_free_pgs;

        // 插入到 Free DB
        rc = mdb_put(txn, FREE_DBI, &key, &data, 0);
        if (rc)
            goto fail;
    }

    // 3. 更新元数据
    txn->mt_dbs[FREE_DBI].md_entries += txn->mt_free_pgs[0];

    mdb_cursor_close(mc);
    return MDB_SUCCESS;

fail:
    mdb_cursor_close(mc);
    return rc;
}
```

---

## 12.4 空间分配

### 从空闲列表分配

```c
static MDB_page *mdb_page_alloc(MDB_txn *txn, unsigned num)
{
    MDB_env *env = txn->mt_env;
    MDB_page *np;
    pgno_t pgno;

    // 1. 优先从事务本地空闲列表
    if (txn->mt_free_pgs[0] > 0) {
        // 从末尾取（LIFO）
        pgno = txn->mt_free_pgs[txn->mt_free_pgs[0]--];
        np = (MDB_page *)((char *)env->me_map + pgno * env->me_psize);
        return np;
    }

    // 2. 尝试从松散页面列表
    if (txn->mt_loose_pgs) {
        np = txn->mt_loose_pgs;
        txn->mt_loose_pgs = NEXT_LOOSE_PAGE(np);
        txn->mt_loose_count--;
        return np;
    }

    // 3. 从 Free DB 分配
    MDB_cursor *mc;
    mdb_cursor_open(txn, FREE_DBI, &mc);

    // 查找最小的可用事务ID
    // （最老的空闲页优先）
    MDB_val key, data;
    rc = mdb_cursor_get(mc, &key, &data, MDB_FIRST);
    if (rc == 0) {
        MDB_IDL idl = data.mv_data;
        if (idl[0] > 0) {
            pgno = idl[idl[0]--];  // 取最后一个页号
            np = (MDB_page *)((char *)env->me_map + pgno * env->me_psize);

            // 更新或删除记录
            if (idl[0] == 0) {
                mdb_cursor_del(mc, 0);
            } else {
                data.mv_size = (idl[0] + 1) * sizeof(pgno_t);
                mdb_cursor_put(mc, &key, &data, MDB_CURRENT);
            }

            mdb_cursor_close(mc);
            return np;
        }
    }
    mdb_cursor_close(mc);

    // 4. 扩展文件
    if (txn->mt_next_pgno + num > env->me_maxpg) {
        return NULL;  // 空间不足
    }

    np = (MDB_page *)((char *)env->me_map +
                      txn->mt_next_pgno * env->me_psize);
    txn->mt_next_pgno += num;

    return np;
}
```

### 松散页面

```c
// 松散页面：已修改但可以立即重用的页面
// 条件：
// 1. 页面已被修改 (P_DIRTY)
// 2. 没有读事务在使用
// 3. 不需要保留到提交后

static void mdb_page_loose(MDB_txn *txn, MDB_page *mp)
{
    // 添加到松散页面列表
    NEXT_LOOSE_PAGE(mp) = txn->mt_loose_pgs;
    txn->mt_loose_pgs = mp;
    txn->mt_loose_count++;

    // 标记页面
    mp->mp_flags |= P_LOOSE;
}
```

---

## 12.5 空间管理策略

### 分配优先级

```
优先级顺序：

1. 松散页面 (mt_loose_pgs)
   原因：可立即重用，无需检查读事务

2. 事务本地空闲列表 (mt_free_pgs)
   原因：本事务刚释放，可能仍在缓存中

3. Free DB
   原因：持久化的空闲页面

4. 扩展文件
   原因：最后手段
```

### 回收策略

```
何时回收页面：

1. 删除节点时
   • 叶子节点：直接回收页面
   • 分支节点：递归回收子树

2. 合并页面时
   • 两个页面合并后，回收一个

3. 删除数据库时
   • 回收所有页面

4. 清理操作时
   • 检查是否有读事务在使用
   • 如果没有，可以回收
```

---

## 12.6 Free DB 的操作

### 读取空闲列表

```c
// 获取所有空闲页面
static int mdb_freelist_read(MDB_txn *txn, MDB_idl *freelist)
{
    MDB_cursor *mc;
    MDB_val key, data;
    MDB_IDL merged = NULL;
    int rc;

    mdb_cursor_open(txn, FREE_DBI, &mc);

    // 遍历所有记录
    rc = mdb_cursor_get(mc, &key, &data, MDB_FIRST);
    while (rc == 0) {
        MDB_IDL idl = data.mv_data;

        // 合并到总列表
        if (!merged) {
            merged = idl;
        } else {
            mdb_midl_append_list(&merged, idl);
        }

        rc = mdb_cursor_get(mc, &key, &data, MDB_NEXT);
    }

    mdb_cursor_close(mc);

    *freelist = merged ? merged : mdb_midl_alloc(16);
    return MDB_SUCCESS;
}
```

### 清理旧记录

```c
// 定期清理 Free DB 中过期的记录
// 这些记录的页面已经被重用
static int mdb_freelist_clean(MDB_txn *txn)
{
    MDB_cursor *mc;
    MDB_val key, data;
    txnid_t oldest;
    int rc;

    // 1. 找到最老的活跃读事务
    oldest = mdb_find_oldest_txnid(txn);

    // 2. 删除所有 txnid < oldest 的记录
    mdb_cursor_open(txn, FREE_DBI, &mc);

    rc = mdb_cursor_get(mc, &key, &data, MDB_FIRST);
    while (rc == 0) {
        txnid_t txnid = *(txnid_t *)key.mv_data;

        if (txnid < oldest) {
            // 这个记录比最老的读事务还老
            // 可以删除
            mdb_cursor_del(mc, 0);
        } else {
            rc = mdb_cursor_get(mc, &key, &data, MDB_NEXT);
        }
    }

    mdb_cursor_close(mc);
    return MDB_SUCCESS;
}
```

---

## 12.7 空间统计

### 获取空间使用情况

```bash
# 使用 mdb_stat 工具
$ mdb_stat testdb

# 输出包含：
# - Map size: 104857600 (100 MB)
# - Page size: 4096
# - Max pages: 25600
# - Number of pages used: 1000
# - Last transaction ID: 50
# - Free pages: 100
# - Leaf pages: 800
# - Branch pages: 50
# - Overflow pages: 50
```

### 编程方式获取

```c
// 获取统计信息
MDB_stat *stat;
mdb_stat(txn, dbi, &stat);

printf("Branch pages: %u\n", stat->ms_branch_pages);
printf("Leaf pages: %u\n", stat->ms_leaf_pages);
printf("Overflow pages: %u\n", stat->ms_overflow_pages);
printf("Entries: %zu\n", stat->ms_entries);

free(stat);
```

---

## 12.8 今日练习

### 练习 1: 观察空间重用

```c
void test_space_reuse(MDB_env *env) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;

    // 1. 插入数据
    mdb_txn_begin(env, NULL, 0, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);

    for (int i = 0; i < 1000; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key_%d", i);
        key = (MDB_val){buf, strlen(buf)};
        data = (MDB_val){"data", 4};
        mdb_put(txn, dbi, &key, &data, 0);
    }
    mdb_txn_commit(txn);

    // 2. 获取统计
    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    MDB_stat stat1;
    mdb_stat(txn, dbi, &stat1);
    printf("After insert: %u pages\n", stat1.ms_branch_pages +
                                    stat1.ms_leaf_pages);
    mdb_txn_abort(txn);

    // 3. 删除一半数据
    mdb_txn_begin(env, NULL, 0, &txn);
    for (int i = 0; i < 500; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key_%d", i);
        key = (MDB_val){buf, strlen(buf)};
        mdb_del(txn, dbi, &key, NULL);
    }
    mdb_txn_commit(txn);

    // 4. 重新插入数据
    mdb_txn_begin(env, NULL, 0, &txn);
    for (int i = 0; i < 500; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key_new_%d", i);
        key = (MDB_val){buf, strlen(buf)};
        data = (MDB_val){"data", 4};
        mdb_put(txn, dbi, &key, &data, 0);
    }
    mdb_txn_commit(txn);

    // 5. 获取统计
    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    MDB_stat stat2;
    mdb_stat(txn, dbi, &stat2);
    printf("After reuse: %u pages\n", stat2.ms_branch_pages +
                                   stat2.ms_leaf_pages);
    mdb_txn_abort(txn);

    // 页面数应该相近（空间被重用）
}
```

---

## 12.9 常见问题解答

### Q1: 为什么有三层空闲列表而不是一个全局列表？

**A:** 分层设计的优势：

```
全局列表的问题：
1. 每次分配/释放都需要操作全局结构
2. 事务间竞争严重
3. 性能随数据量下降

三层列表的优势：
1. 事务空闲列表 (mt_free_pgs)
   - 本事务释放的页面
   - 优先重用（局部性）
   - 无需锁

2. 环境空闲列表 (me_free_pgs)
   - 已提交事务释放的页面
   - 跨事务共享
   - 持久化到 Free DB

3. Free DB
   - 持久化的空闲页号
   - 崩溃后可恢复
   - 延迟更新
```

### Q2: 松散页面和普通空闲页面有什么区别？

**A:** 松散页面的特殊性：

```
普通空闲页面：
- 存储在空闲列表中
- 可以立即重用
- 跨事务共享

松散页面 (mt_loose_pgs)：
- 本事务修改后废弃的页面
- 暂存到事务结束
- 不能立即重用（可能有读事务引用）
- 事务提交后加入空闲列表

示例：
事务A修改页面P5 -> 创建P100
P5变成松散页面（等待事务结束）
事务A提交后 -> P5加入空闲列表
```

### Q3: 如何避免空闲列表无限增长？

**A:** 空闲列表管理策略：

```c
// 定期合并和压缩空闲列表
void mdb_freelist_merge(MDB_txn *txn) {
    // 1. 合并连续页号
    sort_and_coalesce(txn->mt_free_pgs);

    // 2. 去除重复项
    remove_duplicates(txn->mt_free_pgs);

    // 3. 持久化到 Free DB
    mdb_midl_append_list(&free_db, txn->mt_free_pgs);

    // 4. 清理事务列表
    mdb_midl_free(txn->mt_free_pgs);
    txn->mt_free_pgs = empty_list;
}
```

### Q4: 什么情况下需要清理 Free DB？

**A:** Free DB 清理时机：

```
清理触发条件：
1. 空闲列表过大（超过阈值）
   - 影响 MDB_IDL 的效率

2. 频繁的页面释放和分配
   - 造成碎片化

3. 数据库收缩后
   - 大量页面不再使用

清理方法：
1. 合并连续的页号范围
2. 去除重复项
3. 排序优化查找
4. 压缩存储
```

---

## 12.10 思考题

1. 为什么有三层空闲列表而不是一个全局列表？
2. 松散页面和普通空闲页面有什么区别？
3. 如何避免空闲列表无限增长？
4. 什么情况下需要清理 Free DB？
5. 如何优化空间分配的性能？

---

## 明天预告

Day 13 将深入讲解平台特定优化。我们将学习：
- Windows 特定的实现
- Unix 变体的差异
- MIPS 和 ARM 的特殊处理
- 性能优化技巧

不同平台需要不同的优化策略！

---
**参考文献：**
- mdb.c: mdb_page_alloc()
- mdb.c: mdb_page_free()
- mdb.c: mdb_freelist_save()
- midl.c, midl.h (ID List 实现)
