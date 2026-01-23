# LMDB 底层实现 14天课程 - Day 5

## B+树实现 - LMDB 的核心数据结构

欢迎回来！今天我们将深入 LMDB 最核心的部分 - **B+树**。B+树是 LMDB 组织和检索数据的基础，理解它对掌握整个系统至关重要。

---

## 今天的目标

1. 理解 LMDB 中 B+树的结构
2. 掌握节点搜索算法
3. 理解树遍历机制
4. 学习键比较函数

---

## 5.1 B+树结构概览

### LMDB 的 B+树特点

```
┌─────────────────────────────────────────────────────────────┐
│                    LMDB B+树特性                            │
├─────────────────────────────────────────────────────────────┤
│  • 经典 B+树结构                                            │
│  • 分支节点存储键和子页面指针                                 │
│  • 叶子节点存储键和实际数据                                   │
│  • 所有数据都在叶子节点                                      │
│  • 叶子节点之间没有链表（不同于传统 B+树）                     │
│  • 支持 DUPSORT（重复键排序）                                │
│  • 支持固定大小键优化（LEAF2）                                │
└─────────────────────────────────────────────────────────────┘
```

### B+树示例

```
                    ┌─────────────┐
                    │  Root Page  │
                    │   (Branch)  │
                    └──────┬──────┘
                           │
        ┌──────────────────┼──────────────────┐
        │                  │                  │
    ┌───▼────┐        ┌───▼────┐        ┌───▼────┐
    │ Branch │        │ Branch │        │ Leaf   │
    │ Page 1 │        │ Page 2 │        │ Page 3 │
    └───┬────┘        └───┬────┘        └────────┘
        │                 │
    ┌───┴────┐       ┌───┴────┐
    │ Leaf   │       │ Leaf   │
    │ Page 4 │       │ Page 5 │
    └────────┘       └────────┘
```

---

## 5.2 MDB_db - 数据库描述符

### 数据库结构 (mdb.c:1222)

```c
typedef struct MDB_db {
    uint32_t  md_pad;            // 页大小或 LEAF2 键大小
    uint16_t  md_flags;          // 数据库标志
    uint16_t  md_depth;          // 树的深度
    pgno_t    md_branch_pages;   // 分支页数量
    pgno_t    md_leaf_pages;     // 叶子页数量
    pgno_t    md_overflow_pages; // 溢出页数量
    mdb_size_t md_entries;       // 条目数量
    pgno_t    md_root;           // 根页面页号
} MDB_db;
```

### 数据库标志

```c
#define MDB_REVERSEKEY   0x02  // 反向键比较
#define MDB_DUPSORT      0x04  // 排序重复数据
#define MDB_INTEGERKEY   0x08  // 整数键
#define MDB_DUPFIXED     0x10  // 固定大小重复数据
#define MDB_INTEGERDUP   0x20  // 整数重复数据
#define MDB_REVERSEDUP   0x40  // 反向重复数据比较
```

---

## 5.3 节点搜索算法

### mdb_node_search() - 二分查找

这是 B+树的核心搜索函数 (mdb.c:6046)：

```c
static MDB_node *
mdb_node_search(MDB_cursor *mc, MDB_val *key, int *exactp)
{
    unsigned int i = 0, nkeys;
    int low, high;
    int rc = 0;
    MDB_page *mp = mc->mc_pg[mc->mc_top];
    MDB_node *node = NULL;
    MDB_val nodekey;
    MDB_cmp_func *cmp;

    nkeys = NUMKEYS(mp);

    // 分支页从索引 1 开始（索引 0 是最小键的子页）
    low = IS_LEAF(mp) ? 0 : 1;
    high = nkeys - 1;
    cmp = mc->mc_dbx->md_cmp;  // 获取键比较函数

    // 整数键优化
    if (cmp == mdb_cmp_cint && IS_BRANCH(mp)) {
        if (NODEPTR(mp, 1)->mn_ksize == sizeof(mdb_size_t))
            cmp = mdb_cmp_long;
        else
            cmp = mdb_cmp_int;
    }

    // LEAF2 特殊处理
    if (IS_LEAF2(mp)) {
        nodekey.mv_size = mc->mc_db->md_pad;
        node = NODEPTR(mp, 0);  // 假节点
        while (low <= high) {
            i = (low + high) >> 1;  // 中间位置
            nodekey.mv_data = LEAF2KEY(mp, i, nodekey.mv_size);
            rc = cmp(key, &nodekey);
            if (rc == 0) break;
            if (rc > 0)
                low = i + 1;
            else
                high = i - 1;
        }
    } else {
        // 标准二分查找
        while (low <= high) {
            i = (low + high) >> 1;

            node = NODEPTR(mp, i);
            nodekey.mv_size = NODEKSZ(node);
            nodekey.mv_data = NODEKEY(node);

            rc = cmp(key, &nodekey);
            if (rc == 0)
                break;
            if (rc > 0)
                low = i + 1;
            else
                high = i - 1;
        }
    }

    // 如果找到的键小于目标键，移动到下一个
    if (rc > 0) {
        i++;
        if (!IS_LEAF2(mp))
            node = NODEPTR(mp, i);
    }

    if (exactp)
        *exactp = (rc == 0 && nkeys > 0);

    // 存储找到的索引
    mc->mc_ki[mc->mc_top] = i;

    if (i >= nkeys)
        return NULL;  // 没有找到 >= 键的节点

    return node;
}
```

### 二分查找示例

```
在页面中查找键 "hello"：

初始状态：
索引:  0     1      2      3      4      5
键值: "apple" "cat" "grape" "mango" "peach" "zebra"
low = 0, high = 5

第 1 轮：
i = (0 + 5) >> 1 = 2
比较 "hello" 和 "grape"
rc > 0 (hello > grape)
low = 3

第 2 轮：
i = (3 + 5) >> 1 = 4
比较 "hello" 和 "peach"
rc < 0 (hello < peach)
high = 3

第 3 轮：
i = (3 + 3) >> 1 = 3
比较 "hello" 和 "mango"
rc < 0 (hello < mango)
high = 2

循环结束 (low > high)
rc < 0，所以返回索引 3 ("mango")
exactp = 0（非精确匹配）
```

---

## 5.4 页面搜索

### mdb_page_search() - 搜索树

```c
// 搜索标志
#define MDB_PS_ROOTONLY  0x10  // 只定位到根页面
#define MDB_PS_MODIFY    0x04  // 准备修改（写事务）
#define MDB_PS_FIRST     0x02  // 定位到第一个条目
#define MDB_PS_LAST      0x08  // 定位到最后一个条目

static int mdb_page_search(MDB_cursor *mc, MDB_val *key, int flags)
{
    int rc;
    unsigned int top = 0;
    MDB_node *node;

    // 从根开始
    mc->mc_top = 0;
    mc->mc_pg[0] = mdb_get_page(mc->mc_txn, mc->mc_db->md_root);
    mc->mc_ki[0] = 0;

    if (flags & MDB_PS_ROOTONLY)
        return MDB_SUCCESS;

    // 向下遍历到叶子页
    while (IS_BRANCH(mc->mc_pg[top])) {
        MDB_page *mp = mc->mc_pg[top];

        // 搜索分支节点
        if (key) {
            int exact;
            node = mdb_node_search(mc, key, &exact);
            if (!node)
                return MDB_NOTFOUND;  // 键太大
        } else {
            // 没有 key，取第一个或最后一个子页
            if (flags & MDB_PS_LAST)
                mc->mc_ki[top] = NUMKEYS(mp) - 1;
            else
                mc->mc_ki[top] = 0;
            node = NODEPTR(mp, mc->mc_ki[top]);
        }

        // 移动到子页面
        top++;
        mc->mc_top = top;
        mc->mc_pg[top] = mdb_get_page(mc->mc_txn, NODEPGNO(node));
        mc->mc_ki[top] = 0;
    }

    return MDB_SUCCESS;
}
```

### 搜索流程示例

```
查找键 "orange"：

步骤 1：从根页面（分支页）开始
根页面有键："grape", "mango", "peach"
子页：[page 5, page 8, page 10, page 12]

步骤 2：二分查找
比较 "orange" 和 "mango" -> rc > 0
比较 "orange" 和 "peach" -> rc < 0
找到子页面 10

步骤 3：移动到子页面 10（分支页）
子页面 10 有键："lemon", "melon"
子页：[page 20, page 22, page 24]

步骤 4：继续二分查找
比较 "orange" 和 "melon" -> rc > 0
找到子页面 24

步骤 5：移动到子页面 24（叶子页）
叶子页 24 有键："nectarine", "orange", "papaya"

步骤 6：在叶子页中二分查找
比较 "orange" 和 "orange" -> rc = 0
找到！
```

---

## 5.5 键比较函数

### 比较函数类型

```c
typedef int (*MDB_cmp_func)(const MDB_val *a, const MDB_val *b);

// 返回值：
// < 0 : a < b
// = 0 : a = b
// > 0 : a > b
```

### 标准比较函数

```c
// 默认：使用 memcmp 的字典序比较
int mdb_cmp_memn(const MDB_val *a, const MDB_val *b)
{
    int diff;
    ssize_t len_diff;
    len_diff = a->mv_size - b->mv_size;
    if (len_diff) return len_diff;

    diff = memcmp(a->mv_data, b->mv_data, a->mv_size);
    return diff;
}

// 整数比较（小端序）
int mdb_cmp_cint(const MDB_val *a, const MDB_val *b)
{
    return *(unsigned int *)a->mv_data - *(unsigned int *)b->mv_data;
}

// 长整数比较（64位）
int mdb_cmp_long(const MDB_val *a, const MDB_val *b)
{
    return *(mdb_size_t *)a->mv_data - *(mdb_size_t *)b->mv_data;
}

// 反向比较
int mdb_cmp_reverse(const MDB_val *a, const MDB_val *b)
{
    return -mdb_cmp_memn(a, b);
}
```

---

## 5.6 游标栈

### 栈结构

```c
struct MDB_cursor {
    MDB_txn   *mc_txn;           // 所属事务
    MDB_dbi    mc_dbi;           // 数据库句柄
    MDB_db    *mc_db;            // 数据库记录
    MDB_dbx   *mc_dbx;           // 数据库辅助信息
    unsigned char *mc_dbflag;    // 数据库标志

    unsigned short  mc_snum;     // 栈深度
    unsigned short  mc_top;      // 栈顶索引
    unsigned int    mc_flags;    // 游标标志

    // 游标栈
    MDB_page  *mc_pg[CURSOR_STACK];  // 页面指针栈
    indx_t     mc_ki[CURSOR_STACK];  // 页内索引栈
};

#define CURSOR_STACK  32  // 最大树深度
```

### 栈使用示例

```
游标栈状态（查找 "orange" 后）：

索引  页面指针      页内索引
0     [Root Page]  1
1     [Page 10]    1
2     [Page 24]    1   <-- mc_top = 2

mc_snum = 3 (栈中有 3 个元素)
mc_top = 2 (当前在索引 2)
```

---

## 5.7 树遍历

### 向下移动（到第一个条目）

```c
static int mdb_page_search_lowest(MDB_cursor *mc)
{
    int rc;

    mc->mc_top = 0;
    mc->mc_pg[0] = mdb_get_page(mc->mc_txn, mc->mc_db->md_root);
    mc->mc_ki[0] = 0;

    // 向下到最左边的叶子页
    while (IS_BRANCH(mc->mc_pg[mc->mc_top])) {
        MDB_page *mp = mc->mc_pg[mc->mc_top];
        MDB_node *node = NODEPTR(mp, 0);

        mc->mc_top++;
        mc->mc_pg[mc->mc_top] = mdb_get_page(mc->mc_txn, NODEPGNO(node));
        mc->mc_ki[mc->mc_top] = 0;
    }

    return MDB_SUCCESS;
}
```

### 向上移动（到父节点）

```c
static int mdb_cursor_pop(MDB_cursor *mc)
{
    if (mc->mc_top == 0)
        return MDB_NOTFOUND;

    mc->mc_top--;
    return MDB_SUCCESS;
}
```

---

## 5.8 DUPSORT 处理

### 重复键的结构

```
当设置 MDB_DUPSORT 时：

普通叶子节点：
┌────────────────────────────────────┐
│ 键: "color"                         │
│ 数据: "red"                         │
└────────────────────────────────────┘

DUPSORT 叶子节点：
┌────────────────────────────────────┐
│ 键: "color"                         │
│ 标志: F_DUPDATA | F_SUBDATA         │
│ 数据: [子页面号]                    │
└────────────────────────────────────┘
           │
           ▼
    [子页面 - 重复数据 DB]
┌────────────────────────────────────┐
│ "blue" | "green" | "red" | "yellow" │
└────────────────────────────────────┘
```

---

## 5.9 实践：树操作

### 练习 1: 遍历所有键

```c
void traverse_tree(MDB_txn *txn, MDB_dbi dbi) {
    MDB_cursor *cursor;
    MDB_val key, data;
    int rc;

    mdb_cursor_open(txn, dbi, &cursor);

    rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    while (rc == 0) {
        printf("Key: %.*s\n", (int)key.mv_size, (char *)key.mv_data);
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

    // 定位到起始键
    key.mv_data = (void *)start;
    key.mv_size = strlen(start);
    rc = mdb_cursor_get(cursor, &key, &data, MDB_SET_RANGE);

    // 遍历直到结束键
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

## 5.10 完整示例：B+树可视化工具

### tree_visualizer.c - 树结构可视化程序

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lmdb.h>

// 分析树统计信息
void analyze_tree(MDB_txn *txn, MDB_dbi dbi) {
    MDB_stat stat;
    MDB_envinfo info;

    mdb_stat(txn, dbi, &stat);
    mdb_env_info(mdb_txn_env(txn), &info);

    printf("\n========== B+树统计信息 ==========\n");
    printf("页面大小:        %u bytes\n", stat.ms_psize);
    printf("树的深度:        %u\n", stat.ms_depth);
    printf("分支页面数:      %zu\n", stat.ms_branch_pages);
    printf("叶子页面数:      %zu\n", stat.ms_leaf_pages);
    printf("溢出页面数:      %zu\n", stat.ms_overflow_pages);
    printf("总条目数:        %zu\n", stat.ms_entries);
    printf("====================================\n\n");
}

// 测试不同数据量下的树深度
void test_tree_depths() {
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;
    int rc;

    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024 * 100);
    mdb_env_open(env, "./test_depth", 0, 0664);

    printf("\n========== 树深度测试 ==========\n");

    size_t test_sizes[] = {10, 100, 1000, 10000, 100000};
    int num_tests = sizeof(test_sizes) / sizeof(test_sizes[0]);

    for (int i = 0; i < num_tests; i++) {
        size_t count = test_sizes[i];

        system("rm -rf test_depth && mkdir -p test_depth");

        mdb_txn_begin(env, NULL, 0, &txn);
        mdb_dbi_open(txn, NULL, 0, &dbi);

        char key_buf[32], data_buf[32];
        for (size_t j = 0; j < count; j++) {
            snprintf(key_buf, sizeof(key_buf), "key-%08zu", j);
            snprintf(data_buf, sizeof(data_buf), "data-%08zu", j);

            key.mv_data = key_buf;
            key.mv_size = strlen(key_buf);
            data.mv_data = data_buf;
            data.mv_size = strlen(data_buf);

            mdb_put(txn, dbi, &key, &data, 0);
        }

        mdb_txn_commit(txn);

        mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
        MDB_stat stat;
        mdb_stat(txn, dbi, &stat);

        printf("数据量: %8zu 条 | 深度: %u | 分支页: %5zu | 叶子页: %5zu\n",
               count, stat.ms_depth, stat.ms_branch_pages, stat.ms_leaf_pages);

        mdb_txn_abort(txn);
        mdb_dbi_close(env, dbi);
    }

    printf("====================================\n\n");
    mdb_env_close(env);
}

int main() {
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;
    int rc;

    rc = mdb_env_create(&env);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_create failed: %s\n", mdb_strerror(rc));
        return 1;
    }

    mdb_env_set_mapsize(env, 1024 * 1024 * 100);
    mdb_env_set_maxdbs(env, 2);

    rc = mdb_env_open(env, "./testdb", 0, 0664);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_open failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    mdb_txn_begin(env, NULL, 0, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);

    const char *fruits[] = {"apple", "apricot", "banana", "blackberry",
                            "cherry", "cranberry", "date", "dragonfruit",
                            "elderberry", "fig", "grape", "grapefruit",
                            "guava", "kiwi", "lemon", "lime",
                            "mango", "melon", "nectarine", "orange",
                            "papaya", "peach", "pear", "pineapple",
                            "plum", "pomegranate", "raspberry", "strawberry",
                            NULL};

    MDB_val key, data;
    for (int i = 0; fruits[i] != NULL; i++) {
        key.mv_data = (void *)fruits[i];
        key.mv_size = strlen(fruits[i]);
        data.mv_data = (void *)"fruit";
        data.mv_size = 5;
        mdb_put(txn, dbi, &key, &data, 0);
    }

    mdb_txn_commit(txn);

    printf("\n========== B+树可视化 ==========\n");
    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    analyze_tree(txn, dbi);
    mdb_txn_abort(txn);
    mdb_dbi_close(env, dbi);

    test_tree_depths();
    mdb_env_close(env);

    return 0;
}
```

---

## 5.11 常见问题解答

### Q1: 为什么 LMDB 的 B+树叶子页之间没有链表？

**A:** 传统 B+树在叶子页之间维护链表是为了支持快速的范围查询。LMDB 不这样做的原因：

1. **MVCC 冲突**：不同版本的页面可能分散在不同位置
2. **游标栈机制**：可以高效地向上回溯
3. **简化实现**：减少维护成本

### Q2: CURSOR_STACK 为什么是 32？

**A:** 32 层可以支持 2^32 个页面，每页 4KB = 16TB 数据，对实际应用完全够用。

---

## 5.12 今日练习

1. 编译运行 tree_visualizer.c
2. 观察不同数据量下的树深度变化
3. 使用 GDB 调试 mdb_node_search

---

## 明天预告

Day 6 将深入讲解事务管理（上）。

---
**参考文献：**
- mdb.c: 1222-1232 (MDB_db)
- mdb.c: 6046-6134 (mdb_node_search)
- mdb.c: 6666-6750 (mdb_page_search)
- lmdb.h: MDB_cmp_func 相关
