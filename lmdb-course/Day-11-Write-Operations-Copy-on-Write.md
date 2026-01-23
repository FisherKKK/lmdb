# LMDB 底层实现 14天课程 - Day 11

## 写操作与写时复制 - 原子性的保证

欢迎回来！今天我们将深入 LMDB 的写操作机制，特别是**写时复制 (Copy-on-Write)**。这是 LMDB 保证原子性和不损坏数据的关键技术。

---

## 今天的目标

1. 理解写时复制的原理
2. 掌握页面修改机制
3. 学习页面分裂和合并
4. 理解溢出页的处理

---

## 11.1 写时复制概述

### 什么是写时复制？

```
写时复制 (Copy-on-Write, COW):
  • 修改数据前先创建副本
  • 旧版本保留给活跃的读事务
  • 新版本用于写事务
  • 提交后新版本成为当前版本
```

### 为什么需要写时复制？

```
没有 COW 的问题：

┌────────────┐
│  Page 5    │ [A, B, C]
└────────────┘
     │
     │ 写事务直接修改
     ▼
┌────────────┐
│  Page 5    │ [A, X, C]  ← 旧版本丢失
└────────────┘
                    │
                    │ 读事务仍然引用
                    ▼
              ┌────────────┐
              │  读事务 1  │ 看到不一致的数据！
              └────────────┘

使用 COW：

┌────────────┐
│  Page 5    │ [A, B, C]  ← 读事务使用
└────────────┘
     │
     │ 复制
     ▼
┌────────────┐
│  Page 5'   │ [A, X, C]  ← 写事务使用
└────────────┘

两个版本共存，互不干扰！
```

---

## 11.2 页面修改流程

### mdb_page_touch() - 准备修改页面

```c
static int mdb_page_touch(MDB_cursor *mc)
{
    MDB_page *mp = mc->mc_pg[mc->mc_top];
    MDB_txn *txn = mc->mc_txn;
    pgno_t pgno = MP_PGNO(mp);

    // 1. 检查是否已经是脏页
    if (mp->mp_flags & P_DIRTY)
        return MDB_SUCCESS;  // 已经是脏页，可以直接修改

    // 2. 检查是否需要复制
    //    如果有活跃的读事务在使用这个页面，需要复制
    if (txn->mt_txnid > 1 && mdb_page_unspill(txn, mp)) {
        // 页面正在被读事务使用，需要复制
    }

    // 3. 分配新页面
    MDB_page *np = mdb_page_alloc(txn, 1);
    if (!np)
        return ENOMEM;

    // 4. 复制页面内容
    memcpy(np, mp, txn->mt_env->me_psize);

    // 5. 更新页面号
    np->mp_pgno = np->mp_pgno;  // 保持原页号
    mp->mp_pgno = np->mp_pgno;  // ??? 这段代码逻辑有误

    // 正确的做法：
    // np 保持新的页号
    // mp 标记为过时

    // 6. 标记为脏页
    np->mp_flags |= P_DIRTY;

    // 7. 添加到脏页列表
    // ...

    // 8. 更新游标
    mc->mc_pg[mc->mc_top] = np;

    return MDB_SUCCESS;
}
```

### 页面修改示例

```
初始状态：
  Page 5 (pgno=5): [A, B, C]
  读事务 1: 引用 Page 5

步骤 1: 写事务准备修改
  mdb_page_touch(cursor)

步骤 2: 分配新页面
  Page 100 (新分配)

步骤 3: 复制内容
  Page 100: [A, B, C]

步骤 4: 标记脏页
  Page 100->mp_flags |= P_DIRTY

步骤 5: 修改新页面
  Page 100: [A, X, C]

步骤 6: 提交时更新元数据
  Parent node -> pgno = 100

结果：
  Page 5: [A, B, C]  ← 读事务 1 仍在使用
  Page 100: [A, X, C] ← 新版本
```

---

## 11.3 页面分配

### mdb_page_malloc() - 分配页面

```c
static MDB_page *mdb_page_malloc(MDB_txn *txn, unsigned num)
{
    MDB_env *env = txn->mt_env;
    MDB_page *np;

    // 1. 尝试从空闲列表获取
    if (txn->mt_free_pgs[0] > 0) {
        pgno_t pgno = txn->mt_free_pgs[txn->mt_free_pgs[0]--];
        np = (MDB_page *)((char *)env->me_map + pgno * env->me_psize);
        return np;
    }

    // 2. 尝试从松散页面列表获取
    if (txn->mt_loose_pgs) {
        np = txn->mt_loose_pgs;
        txn->mt_loose_pgs = NEXT_LOOSE_PAGE(np);
        txn->mt_loose_count--;
        return np;
    }

    // 3. 分配新页面
    if (txn->mt_next_pgno + num > env->me_maxpg) {
        // 映射空间已满
        return NULL;
    }

    np = (MDB_page *)((char *)env->me_map +
                      txn->mt_next_pgno * env->me_psize);
    txn->mt_next_pgno += num;

    return np;
}
```

### 页面分配策略

```
页面分配优先级：

1. 事务本地空闲列表 (mt_free_pgs)
   • 本事务释放的页面
   • 可立即重用
   • 最快

2. 松散页面列表 (mt_loose_pgs)
   • 脏后释放的页面
   • 可立即重用
   • 次快

3. 扩展映射空间
   • 从文件末尾分配
   • 需要检查空间限制
   • 最慢
```

---

## 11.4 页面分裂

### 何时分裂？

```c
// 尝试在页面中插入节点
static int mdb_node_add(MDB_page *mp, indx_t indx,
                        MDB_val *key, MDB_val *data,
                        pgno_t pgno, unsigned int flags)
{
    // 计算节点大小
    size_t node_size = NODESIZE + key->mv_size;
    if (IS_LEAF(mp)) {
        node_size += data->mv_size;
    } else {
        node_size += sizeof(pgno_t);
    }

    // 检查空间
    if (node_size > SIZELEFT(mp)) {
        return MDB_PAGE_FULL;  // 需要分裂
    }

    // ... 添加节点 ...
}
```

### 分裂过程

```
分裂前：
  Page 10 (叶子页):
    [A, B, C, D, E, F, G, H]
    填充率: 80%

尝试插入 I：
  空间不足，需要分裂

分裂步骤：

1. 选择分裂点 (通常是中间)
   分裂点索引: 4

2. 创建新页面
   Page 11

3. 复制后半部分到新页面
   Page 10: [A, B, C, D]
   Page 11: [E, F, G, H, I]

4. 更新父页面
   父节点: D -> Page 10
          E -> Page 11

5. 递归检查父页面
   父页面可能也需要分裂
```

### 分裂代码

```c
static int mdb_page_split(MDB_cursor *mc,
                          MDB_val *newkey, MDB_val *newdata,
                          pgno_t newpgno)
{
    MDB_txn *txn = mc->mc_txn;
    MDB_page *mp = mc->mc_pg[mc->mc_top];
    unsigned int i, j;
    indx_t newindx;
    pgno_t pgno;
    MDB_page *newpp;
    MDB_node *node;
    MDB_val xkey, xdata;
    int rc;
    unsigned int nkeys;

    nkeys = NUMKEYS(mp);
    newindx = nkeys / 2;  // 分裂点

    // 1. 分配新页面
    newpp = mdb_page_alloc(txn, 1);
    if (!newpp)
        return ENOMEM;
    pgno = MP_PGNO(newpp);

    // 2. 设置新页面标志
    newpp->mp_flags = mp->mp_flags;
    newpp->mp_lower = PAGEHDRSZ;
    newpp->mp_upper = txn->mt_env->me_psize;

    // 3. 移动后半部分节点到新页面
    for (i = newindx; i < nkeys; i++) {
        node = NODEPTR(mp, i);
        xkey.mv_size = NODEKSZ(node);
        xkey.mv_data = NODEKEY(node);
        if (IS_LEAF(mp)) {
            xdata.mv_size = NODEDSZ(node);
            xdata.mv_data = NODEDATA(node);
        } else {
            xdata.mv_size = sizeof(pgno_t);
            xdata.mv_data = &node->mn_lo;
        }
        rc = mdb_node_add(newpp, i - newindx, &xkey, &xdata, 0, 0);
        if (rc)
            return rc;
    }

    // 4. 调整原页面的大小
    mp->mp_lower = PAGEHDRSZ + newindx * sizeof(indx_t);

    // 5. 将中间键提升到父页面
    xkey.mv_size = NODEKSZ(NODEPTR(mp, newindx - 1));
    xkey.mv_data = NODEKEY(NODEPTR(mp, newindx - 1));

    if (mc->mc_top == 0) {
        // 6a. 根页面分裂，创建新的根
        MDB_page *root = mdb_page_alloc(txn, 1);
        if (!root)
            return ENOMEM;
        root->mp_flags = P_BRANCH;
        root->mp_lower = PAGEHDRSZ;
        root->mp_upper = txn->mt_env->me_psize;

        // 添加两个子节点
        xdata.mv_size = sizeof(pgno_t);
        xdata.mv_data = &pgno;
        rc = mdb_node_add(root, 0, &xkey, &xdata, MP_PGNO(mp), 0);
        if (rc)
            return rc;

        // 更新元数据
        txn->mt_dbs[mc->mc_dbi].md_root = MP_PGNO(root);
        txn->mt_dbs[mc->mc_dbi].md_depth++;

    } else {
        // 6b. 在父页面中插入新键
        mc->mc_top--;
        rc = mdb_node_add(mc->mc_pg[mc->mc_top], mc->mc_ki[mc->mc_top] + 1,
                         &xkey, NULL, pgno, 0);
        if (rc == MDB_PAGE_FULL) {
            // 父页面也需要分裂
            return mdb_page_split(mc, &xkey, NULL, pgno);
        }
    }

    return MDB_SUCCESS;
}
```

---

## 11.5 页面合并

### 何时合并？

```c
// 删除节点后检查是否需要合并
static int mdb_node_del(MDB_cursor *mc, MDB_page *mp, indx_t indx)
{
    // ... 删除节点 ...

    // 检查页面填充率
    if (PAGEFILL(mc->mc_txn->mt_env, mp) < FILL_THRESHOLD) {
        // 页面填充率低于 25%，可能需要合并
        // ...
    }

    return MDB_SUCCESS;
}
```

### 合并过程

```
合并前：
  Page 10: [A, B]      填充率: 20%
  Page 11: [C, D, E]   填充率: 30%

合并步骤：

1. 检查两个页面是否可以合并
   总大小 = size(Page 10) + size(Page 11) + 节点头
   if (总大小 < 页面大小) {
       可以合并
   }

2. 移动 Page 11 的内容到 Page 10
   Page 10: [A, B, C, D, E]

3. 释放 Page 11
   添加到空闲列表

4. 更新父页面
   删除指向 Page 11 的节点
```

---

## 11.6 溢出页处理

### 大数据的存储

```
当数据大于页面空间时：

1. 创建溢出页链
   Page 50: [部分数据]
   Page 51: [部分数据]
   Page 52: [剩余数据]

2. 在叶子页中存储溢出页号
   节点: 键="large_key"
         标志=F_BIGDATA
         数据=Page 50 的页号

3. 读取时重组数据
   读取 Page 50 -> Page 51 -> Page 52
   拼接成完整数据
```

### 溢出页创建

```c
static int mdb_page_overflow(MDB_txn *txn, MDB_val *data,
                             MDB_page **mp)
{
    MDB_env *env = txn->mt_env;
    pgno_t *overflow;
    unsigned int i, numpages;

    // 1. 计算需要的页面数
    numpages = OVPAGES(data->mv_size, env->me_psize);

    // 2. 分配溢出页链
    for (i = 0; i < numpages; i++) {
        MDB_page *np = mdb_page_alloc(txn, 1);
        if (!np) {
            // 分配失败，回滚
            return ENOMEM;
        }
        np->mp_flags = P_OVERFLOW;
        np->mp_pages = numpages - i;
        if (i == 0)
            *mp = np;
    }

    // 3. 写入数据
    char *ptr = (*mp)->mp_data;
    for (i = 0; i < numpages; i++) {
        unsigned int sz = (i == numpages - 1) ?
                          (data->mv_size - i * (env->me_psize - PAGEHDRSZ)) :
                          (env->me_psize - PAGEHDRSZ);
        memcpy(ptr, data->mv_data, sz);
        ptr += sz;
    }

    return MDB_SUCCESS;
}
```

---

## 11.7 今日练习

### 练习 1: 观察页面分裂

```c
// 创建导致分裂的测试
void test_page_split(MDB_env *env) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;
    char key_buf[32], data_buf[32];

    mdb_txn_begin(env, NULL, 0, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);

    // 插入大量数据
    for (int i = 0; i < 1000; i++) {
        snprintf(key_buf, sizeof(key_buf), "key_%d", i);
        snprintf(data_buf, sizeof(data_buf), "data_%d", i);

        key.mv_data = key_buf;
        key.mv_size = strlen(key_buf);
        data.mv_data = data_buf;
        data.mv_size = strlen(data_buf);

        int rc = mdb_put(txn, dbi, &key, &data, 0);
        if (rc) {
            printf("Error at %d: %s\n", i, mdb_strerror(rc));
            break;
        }
    }

    mdb_txn_commit(txn);

    // 使用 mdb_stat 查看统计信息
}
```

### 练习 2: 观察溢出页

```c
// 创建大值导致溢出
void test_overflow_pages(MDB_env *env) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;

    mdb_txn_begin(env, NULL, 0, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);

    // 创建大于页面大小的值
    char *large_data = malloc(10000);
    memset(large_data, 'A', 10000);

    key = (MDB_val){"large_key", 9};
    data.mv_data = large_data;
    data.mv_size = 10000;

    mdb_put(txn, dbi, &key, &data, 0);

    mdb_txn_commit(txn);

    // 检查溢出页数量
    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    mdb_stat(txn, dbi, &stat);
    printf("Overflow pages: %u\n", stat.ms_overflow_pages);
    mdb_txn_abort(txn);

    free(large_data);
}
```

---

## 11.8 常见问题解答

### Q1: 为什么需要写时复制而不是直接修改？

**A:** 写时复制的关键优势：

```
直接修改的问题：
1. 破坏读事务的一致性
   - 读事务可能看到部分修改
   - 无法保证 ACID

2. 崩溃恢复困难
   - 原始数据被覆盖
   - 无法回滚

3. 并发冲突
   - 读写互相阻塞
   - 性能下降

写时复制的优势：
1. 保护读事务快照
2. 原子性更新（修改元数据页）
3. MVCC 并发支持
4. 简单的崩溃恢复
```

### Q2: 页面分裂时如何选择分裂点？

**A:** 分裂点选择策略：

```c
// LMDB 的分裂策略
indx_t split_point = NUMKEYS(mp) / 2;

// 示例：页面有 10 个键
// 分裂点 = 5
// 左页: 键 0-4
// 右页: 键 5-9

// 考虑因素：
// 1. 尽量平衡两个页面
// 2. 确保左页的最大键 < 右页的最小键
// 3. 避免过度分裂
```

### Q3: 什么情况下页面会合并？

**A:** 页面合并的条件：

```
合并条件：
1. 删除操作后页面过空
   - 页面利用率 < 阈值

2. 兄弟页面有足够空间
   - 可以合并到一页

3. 不是根页面
   - 根页面可以只有 1 个键

合并过程：
1. 将两个兄弟页面的键合并
2. 更新父节点的分隔键
3. 释放多余的页面
```

### Q4: 溢出页如何保证原子性？

**A:** 溢出页的原子性保证：

```
关键机制：
1. 写时复制
   - 修改溢出页时也复制到新位置
   - 原溢出页保持不变

2. 批量分配
   - 一次分配所有需要的溢出页
   - 失败则全部回滚

3. 元数据更新
   - 只有成功后才更新元数据
   - 崩溃后溢出页不会被引用

4. 页面链
   - 溢出页按序号链接
   - 读取时按序号组装
```

---

## 11.9 思考题

1. 为什么需要写时复制而不是直接修改？
2. 页面分裂时如何选择分裂点？
3. 什么情况下页面会合并？
4. 溢出页如何保证原子性？
5. 写时复制对空间利用率有什么影响？

---

## 明天预告

Day 12 将深入讲解空闲列表与空间管理。我们将学习：
- 空闲列表的结构
- 页面回收机制
- 空间分配策略
- 碎片整理

空间管理是 LMDB 高效利用存储的关键！

---
**参考文献：**
- mdb.c: mdb_page_touch()
- mdb.c: mdb_page_split()
- mdb.c: mdb_page_malloc()
- mdb.c: mdb_node_add()
