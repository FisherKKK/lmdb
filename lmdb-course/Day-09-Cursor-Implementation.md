# LMDB 底层实现 14天课程 - Day 9

## 游标实现 - 数据访问的导航器

欢迎回来！今天我们将深入 LMDB 的数据访问接口 - **游标**。游标是遍历和操作 B+树的核心工具。

---

## 今天的目标

1. 理解游标的结构和作用
2. 掌握游标的创建和销毁
3. 学习游标的移动操作
4. 理解游标与事务的交互

---

## 9.1 游标结构回顾

```c
struct MDB_cursor {
    // === 链接 ===
    MDB_cursor *mc_next;      // 同一数据库上的下一个游标
    MDB_cursor *mc_backup;    // 备份游标（用于恢复）

    // === 所属 ===
    MDB_txn    *mc_txn;       // 所属事务
    MDB_dbi     mc_dbi;       // 数据库句柄

    // === 数据库信息 ===
    MDB_db     *mc_db;        // 数据库记录
    MDB_dbx    *mc_dbx;       // 数据库辅助信息
    unsigned char *mc_dbflag; // 数据库标志

    // === 栈 ===
    unsigned short mc_snum;   // 栈深度
    unsigned short mc_top;    // 栈顶索引
    MDB_page    *mc_pg[CURSOR_STACK];  // 页面栈
    indx_t       mc_ki[CURSOR_STACK];  // 索引栈

    // === 状态 ===
    unsigned int mc_flags;    // 游标标志

    // === 子游标 (DUPSORT) ===
    struct MDB_xcursor *mc_xcursor;

#ifdef MDB_VL32
    MDB_page *mc_ovpg;       // 引用的溢出页
#endif
};
```

---

## 9.2 游标标志

```c
#define C_INITIALIZED  0x01  // 游标已初始化
#define C_EOF          0x02  // 到达末尾
#define C_SUB          0x04  // 子游标
#define C_DEL          0x08  // 最后一次操作是删除
#define C_UNTRACK      0x40  // 关闭时取消跟踪
#define C_WRITEMAP     MDB_TXN_WRITEMAP  // 写映射模式
#define C_ORIG_RDONLY  MDB_TXN_RDONLY  // 原始只读模式
```

---

## 9.3 游标创建

### mdb_cursor_open() (mdb.c:8765)

```c
int mdb_cursor_open(MDB_txn *txn, MDB_dbi dbi, MDB_cursor **ret)
{
    MDB_cursor *mc;

    // 1. 检查事务状态
    if (txn->mt_flags & MDB_TXN_BLOCKED)
        return MDB_BAD_TXN;

    // 2. 分配游标
    mc = mdb_cursor_alloc(txn, dbi);
    if (!mc)
        return ENOMEM;

    // 3. 初始化游标字段
    mc->mc_txn = txn;
    mc->mc_dbi = dbi;
    mc->mc_db = &txn->mt_dbs[dbi];
    mc->mc_dbx = &txn->mt_dbxs[dbi];
    mc->mc_dbflag = &txn->mt_dbflags[dbi];
    mc->mc_snum = 0;
    mc->mc_top = 0;
    mc->mc_flags = 0;

    // 4. 对于写事务，加入游标列表
    if (!(txn->mt_flags & MDB_TXN_RDONLY)) {
        mc->mc_next = txn->mt_cursors[dbi];
        txn->mt_cursors[dbi] = mc;
    }

    // 5. 初始化子游标（如果需要）
    if (mc->mc_db->md_flags & MDB_DUPSORT) {
        mc->mc_xcursor = malloc(sizeof(MDB_xcursor));
        if (!mc->mc_xcursor) {
            mdb_cursor_close(mc);
            return ENOMEM;
        }
        memset(mc->mc_xcursor, 0, sizeof(MDB_xcursor));
    } else {
        mc->mc_xcursor = NULL;
    }

    *ret = mc;
    return MDB_SUCCESS;
}
```

### 游标分配

```c
static MDB_cursor *mdb_cursor_alloc(MDB_txn *txn, MDB_dbi dbi)
{
    MDB_cursor *mc;

    // 1. 尝试从空闲列表获取
    if (txn->mt_env->me_cursors) {
        mc = txn->mt_env->me_cursors;
        txn->mt_env->me_cursors = mc->mc_next;
    } else {
        // 2. 分配新的
        mc = malloc(sizeof(MDB_cursor));
        if (!mc)
            return NULL;
    }

    return mc;
}
```

---

## 9.4 游标移动操作

### mdb_cursor_get() 操作码

```c
#define MDB_FIRST          0  // 第一个条目
#define MDB_FIRST_DUP      1  // 第一个重复数据
#define MDB_GET_CURRENT    8  // 当前条目
#define MDB_GET_MULTIPLE   9  // 多个条目
#define MDB_LAST          10  // 最后一个条目
#define MDB_LAST_DUP      11  // 最后一个重复数据
#define MDB_NEXT          12  // 下一个条目
#define MDB_NEXT_DUP      13  // 下一个重复数据
#define MDB_NEXT_MULTIPLE 14  // 下多个条目
#define MDB_NEXT_NODUP    15  // 下一个非重复键
#define MDB_PREV          16  // 上一个条目
#define MDB_PREV_DUP      17  // 上一个重复数据
#define MDB_PREV_NODUP    18  // 上一个非重复键
#define MDB_SET           2  // 设置键
#define MDB_SET_KEY       5  // 设置键（返回键）
#define MDB_SET_RANGE     3  // 设置范围
```

### mdb_cursor_get() 实现 (mdb.c:7425)

```c
int mdb_cursor_get(MDB_cursor *mc, MDB_val *key, MDB_val *data,
                   MDB_cursor_op op)
{
    int rc;
    MDB_node *leaf;

    switch (op) {
    case MDB_FIRST:
        // 定位到第一个条目
        rc = mdb_page_search_lowest(mc);
        if (rc)
            return rc;
        mc->mc_ki[mc->mc_top] = 0;
        break;

    case MDB_LAST:
        // 定位到最后一个条目
        rc = mdb_page_search_highest(mc);
        if (rc)
            return rc;
        mc->mc_ki[mc->mc_top] = NUMKEYS(mc->mc_pg[mc->mc_top]) - 1;
        break;

    case MDB_NEXT:
        // 移动到下一个条目
        if (!(mc->mc_flags & C_INITIALIZED))
            return mdb_cursor_get(mc, key, data, MDB_FIRST);

        rc = mdb_cursor_next(mc, key, data, 0);
        if (rc)
            return rc;
        break;

    case MDB_PREV:
        // 移动到上一个条目
        if (!(mc->mc_flags & C_INITIALIZED))
            return mdb_cursor_get(mc, key, data, MDB_LAST);

        rc = mdb_cursor_prev(mc, key, data);
        if (rc)
            return rc;
        break;

    case MDB_SET:
        // 设置到指定键
        rc = mdb_cursor_set(mc, key, data, 0, NULL);
        if (rc)
            return rc;
        break;

    case MDB_SET_RANGE:
        // 设置到指定范围
        rc = mdb_cursor_set(mc, key, data, MDB_SET_RANGE, NULL);
        if (rc)
            return rc;
        break;

    // ... 其他操作
    }

    // 获取键和数据
    leaf = mdb_node_search(mc, key, NULL);
    if (leaf) {
        MDB_GET_KEY(leaf, key);
        if (data) {
            // 获取数据...
        }
    }

    mc->mc_flags |= C_INITIALIZED;
    return MDB_SUCCESS;
}
```

---

## 9.5 游标栈操作

### 下一个条目

```c
static int mdb_cursor_next(MDB_cursor *mc, MDB_val *key,
                           MDB_val *data, MDB_cursor_op op)
{
    MDB_page *mp;
    int rc;

    mp = mc->mc_pg[mc->mc_top];

    // 尝试在当前页面向右移动
    if (mc->mc_ki[mc->mc_top] + 1u < NUMKEYS(mp)) {
        mc->mc_ki[mc->mc_top]++;
    } else {
        // 当前页面已到末尾，需要向上
        while (mc->mc_top > 0) {
            mc->mc_top--;
            mp = mc->mc_pg[mc->mc_top];

            // 尝试在父页面向右移动
            if (mc->mc_ki[mc->mc_top] + 1u < NUMKEYS(mp)) {
                mc->mc_ki[mc->mc_top]++;
                // 向下找到最左边的叶子节点
                rc = mdb_cursor_search_lowest(mc);
                if (rc)
                    return rc;
                goto found;
            }
        }

        // 已经到达最右边
        mc->mc_flags |= C_EOF;
        return MDB_NOTFOUND;
    }

found:
    // 获取键和数据
    return mdb_cursor_get_current(mc, key, data);
}
```

### 上一个条目

```c
static int mdb_cursor_prev(MDB_cursor *mc, MDB_val *key,
                           MDB_val *data)
{
    MDB_page *mp;
    int rc;

    // 尝试在当前页面向左移动
    if (mc->mc_ki[mc->mc_top] > 0) {
        mc->mc_ki[mc->mc_top]--;
    } else {
        // 当前页面已到开头，需要向上
        while (mc->mc_top > 0) {
            mc->mc_top--;
            mp = mc->mc_pg[mc->mc_top];

            if (mc->mc_ki[mc->mc_top] > 0) {
                mc->mc_ki[mc->mc_top]--;
                // 向下找到最右边的叶子节点
                rc = mdb_cursor_search_highest(mc);
                if (rc)
                    return rc;
                goto found;
            }
        }

        return MDB_NOTFOUND;
    }

found:
    mc->mc_flags &= ~C_EOF;
    return mdb_cursor_get_current(mc, key, data);
}
```

---

## 9.6 游标与写事务

### 写事务跟踪游标

```c
// 写事务维护每个数据库的游标列表
struct MDB_txn {
    // ...
    MDB_cursor **mt_cursors;  // mt_cursors[dbi] = 游标列表头
    // ...
};

// 添加游标到列表
mc->mc_next = txn->mt_cursors[dbi];
txn->mt_cursors[dbi] = mc;

// 移除游标
MDB_cursor **prev = &txn->mt_cursors[mc->mc_dbi];
while (*prev && *prev != mc)
    prev = &(*prev)->mc_next;
if (*prev)
    *prev = mc->mc_next;
```

### 页面修改时更新游标

```c
// 当页面分裂或合并时，需要更新所有相关游标
static void mdb_cursor_update(MDB_page *old_page, MDB_page *new_page)
{
    MDB_txn *txn = /* 获取事务 */;
    MDB_cursor *mc;

    // 遍历所有游标
    for (mc = txn->mt_cursors[dbi]; mc; mc = mc->mc_next) {
        // 检查游标是否在受影响的页面上
        for (unsigned i = 0; i <= mc->mc_top; i++) {
            if (mc->mc_pg[i] == old_page) {
                // 更新游标
                mc->mc_pg[i] = new_page;
                // 调整索引...
            }
        }
    }
}
```

---

## 9.7 游标关闭

### mdb_cursor_close() (mdb.c:8855)

```c
int mdb_cursor_close(MDB_cursor *mc)
{
    if (!mc)
        return 0;

    // 1. 从事务的游标列表中移除
    if (!(mc->mc_txn->mt_flags & MDB_TXN_RDONLY)) {
        MDB_cursor **prev = &mc->mc_txn->mt_cursors[mc->mc_dbi];
        while (*prev && *prev != mc)
            prev = &(*prev)->mc_next;
        if (*prev)
            *prev = mc->mc_next;
    }

    // 2. 关闭子游标
    if (mc->mc_xcursor) {
        if (mc->mc_xcursor->mx_cursor.mc_flags & C_INITIALIZED)
            mdb_cursor_close(&mc->mc_xcursor->mx_cursor);
        free(mc->mc_xcursor);
    }

    // 3. 释放游标到空闲列表或直接释放
    if (mc->mc_flags & C_UNTRACK) {
        free(mc);
    } else {
        mc->mc_next = mc->mc_txn->mt_env->me_cursors;
        mc->mc_txn->mt_env->me_cursors = mc;
    }

    return MDB_SUCCESS;
}
```

---

## 9.8 子游标 (DUPSORT)

### MDB_xcursor 结构

```c
typedef struct MDB_xcursor {
    MDB_cursor mx_cursor;   // 子游标
    MDB_db    mx_db;        // 子数据库记录
    MDB_dbx   mx_dbx;       // 子数据库辅助信息
    unsigned char mx_dbflag; // 子数据库标志
} MDB_xcursor;
```

### 子游标使用

```
主数据库 (DUPSORT):
  键: "color"
  标志: F_DUPDATA | F_SUBDATA
  数据: [子页面号]

子页面 (重复数据库):
  "blue"
  "green"
  "red"
  "yellow"

子游标 mx_cursor 用于遍历重复数据：
  MDB_SET -> 定位到 "color"
  MDB_NEXT_DUP -> "blue"
  MDB_NEXT_DUP -> "green"
  MDB_NEXT_DUP -> "red"
  MDB_NEXT_DUP -> "yellow"
  MDB_NEXT_DUP -> MDB_NOTFOUND
```

---

## 9.9 实践：使用游标

### 练习 1: 遍历所有数据

```c
void traverse_all(MDB_txn *txn, MDB_dbi dbi) {
    MDB_cursor *cursor;
    MDB_val key, data;
    int rc;

    mdb_cursor_open(txn, dbi, &cursor);

    rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    while (rc == 0) {
        printf("Key: %.*s, Data: %.*s\n",
               (int)key.mv_size, (char *)key.mv_data,
               (int)data.mv_size, (char *)data.mv_data);
        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
}
```

### 练习 2: 范围查询

```c
void range_query(MDB_txn *txn, MDB_dbi dbi,
                 const char *start, const char *end) {
    MDB_cursor *cursor;
    MDB_val key, data;
    int rc;

    mdb_cursor_open(txn, dbi, &cursor);

    key.mv_data = (void *)start;
    key.mv_size = strlen(start);

    rc = mdb_cursor_get(cursor, &key, &data, MDB_SET_RANGE);
    while (rc == 0) {
        if (strcmp(key.mv_data, end) > 0)
            break;
        printf("Key: %.*s\n", (int)key.mv_size, (char *)key.mv_data);
        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
}
```

---

## 9.10 常见问题解答

### Q1: 为什么游标需要维护一个页面栈？

**A:** 页面栈的作用：

```
B+树结构：
         Root (深度 2)
        /          \
    Branch1      Branch2 (深度 1)
   /  |   \      /  |  \
  L1  L2  L3    L4  L5  L6 (深度 0)

游标栈状态（定位到 L5）：
  mc_pg[0] = Root
  mc_pg[1] = Branch2
  mc_pg[2] = L5          <- mc_top = 2
  mc_ki[0] = 1
  mc_ki[1] = 2
  mc_ki[2] = 0

作用：
1. 向上回溯：移动到父节点或兄弟节点
2. 树遍历：范围查询需要在不同层移动
3. 插入/删除：需要修改从根到叶的路径
```

### Q2: 写事务为什么要跟踪所有游标？

**A:** 原因：

1. **页面修改通知**：当页面被修改时，通知使用该页面的游标
2. **游标失效处理**：更新游标栈中的页面指针
3. **事务提交/中止**：清理相关游标

```c
// 修改页面后更新游标
for (each cursor in txn->mt_cursors) {
    if (cursor uses the modified page) {
        update cursor's page stack;
    }
}
```

### Q3: 子游标如何处理重复数据？

**A:** DUPSORT 的子游标机制：

```
主节点:
  键: "color"
  标志: F_DUPDATA | F_SUBDATA
  数据: 指向子页面

子游标 (mx_cursor):
  - 是一个独立的 MDB_cursor
  - 用于遍历子页面中的重复数据
  - 操作: MDB_NEXT_DUP, MDB_PREV_DUP
```

### Q4: 游标的 C_EOF 标志有什么作用？

**A:** C_EOF 标志表示游标已到末尾：

```c
#define C_EOF 0x01  // 游标在末尾

// 使用场景
if (mc->mc_flags & C_EOF) {
    return MDB_NOTFOUND;
}

// 设置时机
1. MDB_FIRST 失败（数据库为空）
2. MDB_LAST 失败
3. MDB_NEXT 超过最后一条记录
4. MDB_PREV 超过第一条记录
```

---

## 9.11 思考题

1. 为什么游标需要维护一个页面栈？
2. 写事务为什么要跟踪所有游标？
3. 子游标如何处理重复数据？
4. 游标的 C_EOF 标志有什么作用？
5. 如何高效使用游标进行批量操作？

---

## 明天预告

Day 10 将深入讲解锁管理。我们将学习：
- 读者锁的实现
- 写入者锁的实现
- 不同平台的锁机制
- 死锁处理

锁是并发控制的基础！

---
**参考文献：**
- mdb.c: 1419-1490 (MDB_cursor, MDB_xcursor)
- mdb.c: 8765-8850 (mdb_cursor_open, mdb_cursor_close)
- mdb.c: 7425-7500 (mdb_cursor_get)
