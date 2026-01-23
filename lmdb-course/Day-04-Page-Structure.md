# LMDB 底层实现 14天课程 - Day 4

## 页面结构与布局 - LMDB 的存储单元

欢迎回来！今天我们将深入 LMDB 最基础的存储单元 - **页面**。理解页面结构是掌握 B+树操作、事务管理和空间管理的基础。

---

## 今天的目标

1. 理解 MDB_page 结构体
2. 掌握页面类型和标志
3. 理解页面内部布局
4. 学习节点在页面中的组织方式

---

## 4.1 MDB_page 结构体

### 页面定义 (mdb.c:1003)

```c
typedef struct MDB_page {
#define mp_pgno    mp_p.p_pgno    // 页号
#define mp_next    mp_p.p_next    // 下一个空闲页（用于内存列表）
    union {
        pgno_t             p_pgno;   // 页面编号
        struct MDB_page   *p_next;  // 用于空闲页面链表
    } mp_p;

    uint16_t  mp_pad;      // 键大小（仅 LEAF2 页面使用）

    // === 页面标志 ===
#define P_BRANCH   0x01    // 分支页面
#define P_LEAF     0x02    // 叶子页面
#define P_OVERFLOW 0x04    // 溢出页面
#define P_META     0x08    // 元数据页面
#define P_DIRTY    0x10    // 脏页面
#define P_LEAF2    0x20    // 固定大小重复数据页
#define P_SUBP     0x40    // 子页面（用于 DUPSORT）
#define P_LOOSE    0x4000  // 页面已脏后释放，可重用
#define P_KEEP     0x8000  // 页面溢出时保留此页面
    uint16_t  mp_flags;    // 页面类型标志

    // === 空间管理 ===
#define mp_lower   mp_pb.pb.pb_lower  // 空闲空间下界
#define mp_upper   mp_pb.pb.pb_upper  // 空闲空间上界
#define mp_pages   mp_pb.pb_pages     // 溢出页数量
    union {
        struct {
            indx_t  pb_lower;  // 空闲空间下界
            indx_t  pb_upper;  // 空闲空间上界
        } pb;
        uint32_t  pb_pages;    // 溢出页链的页数
    } mp_pb;

    // === 节点指针数组 ===
    indx_t  mp_ptrs[0];    // 动态大小的节点指针数组
} MDB_page;
```

---

## 4.2 页面类型详解

### 页面类型分类

```
┌─────────────────────────────────────────────────────────────┐
│                        页面类型                              │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  P_META (0x08)                                              │
│  ├─ 页 0 和 页 1                                            │
│  ├─ 存储元数据（MDB_meta）                                   │
│  └─ 不包含节点                                              │
│                                                             │
│  P_BRANCH (0x01)                                            │
│  ├─ B+树的内部节点                                          │
│  ├─ 节点包含键和子页面页号                                    │
│  └─ 用于导航到叶子页                                        │
│                                                             │
│  P_LEAF (0x02)                                              │
│  ├─ B+树的叶子节点                                          │
│  ├─ 节点包含键和实际数据                                     │
│  └─ 用户数据存储在这里                                      │
│                                                             │
│  P_LEAF2 (0x20)                                             │
│  ├─ 优化的叶子页（用于固定大小键）                            │
│  ├─ 键连续存储，无节点头                                     │
│  └─ 用于 MDB_DUPFIXED                                      │
│                                                             │
│  P_OVERFLOW (0x04)                                          │
│  ├─ 存储超大值                                              │
│  ├─ 链式结构（mp_pages 指向长度）                            │
│  └─ 数据 > 页面大小 - 页头时使用                             │
│                                                             │
│  P_SUBP (0x40)                                              │
│  ├─ 子页面（用于 DUPSORT）                                   │
│  └─ 嵌入在叶子节点的数据中                                   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 页面大小

```c
// 典型页面大小
#define PAGESIZE  4096   // x86/x86_64
// 其他平台可能是 8192, 16384 等

// 页面头大小（不包括动态数组）
#define PAGEHDRSZ  (unsigned) offsetof(MDB_page, mp_ptrs)
// 通常是 16 字节（2 * pgno_t + 2 * uint16_t + 2 * indx_t）
```

---

## 4.3 页面内部布局

### 非溢出页布局

```
┌─────────────────────────────────────────────────────────────┐
│  页面                                                       │
│  ├─ 页头 (PAGEHDRSZ = 16 bytes)                             │
│  │  ├─ mp_pgno      (4/8 bytes)  页号                       │
│  │  ├─ mp_pad       (2 bytes)    填充                       │
│  │  ├─ mp_flags     (2 bytes)    标志                       │
│  │  ├─ mp_lower     (2 bytes)    空间下界                   │
│  │  └─ mp_upper     (2 bytes)    空间上界                   │
│  ├─ 节点指针数组 (从 PAGEHDRSZ 向上增长)                      │
│  │  ├─ mp_ptrs[0]   ─────┐                                 │
│  │  ├─ mp_ptrs[1]   ────┼──> 指向节点的偏移量               │
│  │  └─ mp_ptrs[n-1] ─────┘                                 │
│  ├─ 空闲空间                                               │
│  │  └─ 在 mp_lower 和 mp_upper 之间                         │
│  └─ 节点数据区 (从页尾向下增长)                              │
│     ├─ 节点 n-1 ────────┐                                  │
│     ├─ 节点 n-2 ───────┼──> 实际键值数据                    │
│     └─ 节点 0 ─────────┘                                  │
└─────────────────────────────────────────────────────────────┘
     低地址                                            高地址
```

### 内存视图（4KB 页面）

```
地址      │ 内容
----------+--------------------------------------------------
0x0000    │ [mp_pgno: 4 bytes]  页号 = 0
0x0004    │ [mp_pad: 2 bytes]   填充
0x0006    │ [mp_flags: 2 bytes] P_LEAF
0x0008    │ [mp_lower: 2 bytes] 指向节点指针数组末尾
0x000A    │ [mp_upper: 2 bytes] 指向节点数据区开头
0x000C    │ [mp_ptrs[0]: 2 bytes] = 4080 (第一个节点偏移)
0x000E    │ [mp_ptrs[1]: 2 bytes] = 4020
0x0010    │ [mp_ptrs[2]: 2 bytes] = 3960
...       │ ... 更多节点指针
0x0FE0    │ [节点 0 的数据] 键+值
0x0FBC    │ [节点 1 的数据]
0x0F80    │ [节点 2 的数据]
...       │ ... 更多节点数据
0x0FF0    │ [最后一个节点]
0x1000    │ 页结束 (4096 字节)
```

---

## 4.4 空间管理

### 关键宏

```c
// 页头大小
#define PAGEHDRSZ  ((unsigned) offsetof(MDB_page, mp_ptrs))

// 元数据指针（跳过页头）
#define METADATA(p)  ((void *)((char *)(p) + PAGEHDRSZ))

// 页面基址（处理 65536 字节页面）
#define PAGEBASE  ((MDB_DEVEL) ? PAGEHDRSZ : 0)

// 节点数量
#define NUMKEYS(p)  ((MP_LOWER(p) - (PAGEHDRSZ-PAGEBASE)) >> 1)
// 解释：(lower - PAGEHDRSZ) / 2，因为每个指针占 2 字节

// 剩余空间
#define SIZELEFT(p)  (indx_t)(MP_UPPER(p) - MP_LOWER(p))

// 页面填充率（千分比）
#define PAGEFILL(env, p)  (1000L * ((env)->me_psize - PAGEHDRSZ - SIZELEFT(p)) / \
                           ((env)->me_psize - PAGEHDRSZ))

// 填充阈值（低于此值页面需要合并）
#define FILL_THRESHOLD  250  // 25%
```

### 空间增长方向

```c
// 节点指针从下向上增长
mp_lower += 2;  // 添加一个指针，lower 增加 2 字节

// 节点数据从上向下增长
mp_upper -= node_size;  // 添加节点，upper 减少

// 检查是否有足够空间
if (mp_lower >= mp_upper) {
    // 页面满了！
    return MDB_PAGE_FULL;
}
```

---

## 4.5 节点结构 (MDB_node)

### 节点定义 (mdb.c:1113)

```c
typedef struct MDB_node {
    // === 数据大小或页号 ===
#if BYTE_ORDER == LITTLE_ENDIAN
    unsigned short  mn_lo;   // 低 16 位
    unsigned short  mn_hi;   // 高 16 位
#else
    unsigned short  mn_hi;   // 大端序
    unsigned short  mn_lo;
#endif

    // === 节点标志 ===
#define F_BIGDATA  0x01  // 数据在溢出页
#define F_SUBDATA  0x02  // 数据是子数据库
#define F_DUPDATA  0x04  // 数据有重复
    unsigned short  mn_flags;

    // === 键大小 ===
    unsigned short  mn_ksize;

    // === 键和数据紧随其后 ===
    char  mn_data[1];
} MDB_node;
```

### 节头大小

```c
#define NODESIZE  offsetof(MDB_node, mn_data)
// 大小：2 + 2 + 2 + 2 = 8 字节
```

### 节点内存布局

```
┌─────────────────────────────────────────────────────────────┐
│  MDB_node                                                   │
│  ├─ mn_lo        (2 bytes)  数据大小低16位                 │
│  ├─ mn_hi        (2 bytes)  数据大小高16位                 │
│  ├─ mn_flags     (2 bytes)  节点标志                       │
│  ├─ mn_ksize     (2 bytes)  键大小                         │
│  └─ mn_data      (变长)     键 + 数据                       │
│     ├─ 键数据    (mn_ksize bytes)                          │
│     └─ 数据      (mn_lo|mn_hi bytes 或 溢出页号)             │
└─────────────────────────────────────────────────────────────┘
```

---

## 4.6 访问宏

### 访问页面内容

```c
// 获取页面指针
#define NODEPTR(p, i)  ((MDB_node *)((char *)(p) + MP_PTRS(p)[i] + PAGEBASE))

// 获取节点键
#define NODEKEY(node)   (void *)((node)->mn_data)

// 获取节点数据
#define NODEDATA(node)  (void *)((char *)(node)->mn_data + (node)->mn_ksize)

// 获取分支节点的子页面号
#define NODEPGNO(node)  \
    ((node)->mn_lo | ((pgno_t)(node)->mn_hi << 16) | \
     (PGNO_TOPWORD ? ((pgno_t)(node)->mn_flags << PGNO_TOPWORD) : 0))

// 设置分支节点的子页面号
#define SETPGNO(node, pgno)  do { \
    (node)->mn_lo = (pgno) & 0xffff; \
    (node)->mn_hi = (pgno) >> 16; \
    if (PGNO_TOPWORD) (node)->mn_flags = (pgno) >> PGNO_TOPWORD; \
} while (0)

// 获取叶子节点的数据大小
#define NODEDSZ(node)  ((node)->mn_lo | ((unsigned)(node)->mn_hi << 16))

// 设置叶子节点的数据大小
#define SETDSZ(node, size)  do { \
    (node)->mn_lo = (size) & 0xffff; \
    (node)->mn_hi = (size) >> 16; \
} while (0)

// 获取键大小
#define NODEKSZ(node)  ((node)->mn_ksize)
```

---

## 4.7 LEAF2 页面

### LEAF2 的特殊布局

```
┌─────────────────────────────────────────────────────────────┐
│  LEAF2 页面（固定大小键）                                    │
│  ├─ 页头 (PAGEHDRSZ)                                        │
│  ├─ 无节点指针数组                                          │
│  └─ 连续存储的键                                            │
│     ├─ 键 0 (固定大小)                                      │
│     ├─ 键 1 (固定大小)                                      │
│     └─ 键 n (固定大小)                                      │
└─────────────────────────────────────────────────────────────┘
```

```c
// LEAF2 键访问宏
#define LEAF2KEY(p, i, ks)  ((char *)(p) + PAGEHDRSZ + ((i) * (ks)))

// 检查页面类型
#define IS_LEAF(p)    F_ISSET(MP_FLAGS(p), P_LEAF)
#define IS_LEAF2(p)   F_ISSET(MP_FLAGS(p), P_LEAF2)
#define IS_BRANCH(p)  F_ISSET(MP_FLAGS(p), P_BRANCH)
#define IS_OVERFLOW(p) F_ISSET(MP_FLAGS(p), P_OVERFLOW)
#define IS_SUBP(p)    F_ISSET(MP_FLAGS(p), P_SUBP)
```

---

## 4.8 溢出页面

### 溢出页结构

```
┌─────────────────────────────────────────────────────────────┐
│  溢出页面链                                                  │
│  ├─ 主页面中的节点只存储溢出页的页号                           │
│  └─ 实际数据存储在溢出页链中                                  │
│                                                             │
│  [主页面节点] ───┐                                           │
│                ├─> [溢出页 1] ───┐                          │
│                └─> [溢出页 2] ───┼──> [溢出页 3] ...         │
│                                                     │       │
└─────────────────────────────────────────────────────┴───────┘
```

```c
// 溢出页数量计算
#define OVPAGES(size, psize)  ((PAGEHDRSZ - 1 + (size)) / (psize) + 1)

// 示例：5000 字节数据，4096 字节页
// OVPAGES(5000, 4096) = (15 + 5000) / 4096 + 1 = 5015 / 4096 + 1 = 2
```

### 溢出页标志使用

```c
// 节点中的 F_BIGDATA 标志表示数据在溢出页
if (node->mn_flags & F_BIGDATA) {
    // mn_lo|mn_hi 存储的是溢出链的第一个页号
    pgno_t ovpg = NODEDSZ(node);
    // 读取溢出页...
}
```

---

## 4.9 页面操作示例

### 添加节点到页面

```c
int mdb_node_add(MDB_page *mp, indx_t indx,
                 MDB_val *key, MDB_val *data,
                 pgno_t pgno, unsigned int flags)
{
    unsigned int i;
    size_t node_size = NODESIZE + key->mv_size;
    indx_t ofs;

    // 计算节点大小
    if (IS_LEAF(mp)) {
        if (F_ISSET(flags, F_BIGDATA)) {
            // 数据页号
            node_size += sizeof(pgno_t);
        } else {
            // 实际数据
            node_size += data->mv_size;
        }
    } else {
        // 分支节点：只有键，数据是页号（存储在 mn_lo/hi）
        node_size += sizeof(pgno_t);
    }

    // 检查空间
    if (node_size > SIZELEFT(mp))
        return MDB_PAGE_FULL;

    // 移动现有节点指针（为新指针腾出空间）
    if (indx < NUMKEYS(mp)) {
        memmove(&mp_ptrs[indx + 1], &mp_ptrs[indx],
                (NUMKEYS(mp) - indx) * sizeof(indx_t));
    }

    // 设置新指针
    mp_upper -= node_size;
    mp_ptrs[indx] = mp_upper;

    // 初始化节点
    node = NODEPTR(mp, indx);
    node->mn_ksize = key->mv_size;
    node->mn_flags = flags;
    memcpy(NODEKEY(node), key->mv_data, key->mv_size);

    // 设置数据
    if (IS_LEAF(mp)) {
        if (F_ISSET(flags, F_BIGDATA)) {
            memcpy(NODEDATA(node), &pgno, sizeof(pgno_t));
        } else {
            memcpy(NODEDATA(node), data->mv_data, data->mv_size);
            SETDSZ(node, data->mv_size);
        }
    } else {
        SETPGNO(node, pgno);
    }

    mp_lower += 2;
    return MDB_SUCCESS;
}
```

---

## 4.10 页面检查工具

### 调试宏

```c
// 打印页面信息
#define DUMP_PAGE(mp)  \
    printf("Page %u: flags=0x%04x, lower=%u, upper=%u, keys=%u\n", \
           MP_PGNO(mp), MP_FLAGS(mp), MP_LOWER(mp), MP_UPPER(mp), NUMKEYS(mp))

// 检查页面完整性
static int check_page(MDB_page *mp) {
    if (MP_LOWER(mp) < PAGEHDRSZ) return -1;
    if (MP_UPPER(mp) > 0xffff) return -1;
    if (MP_LOWER(mp) > MP_UPPER(mp)) return -1;
    return 0;
}
```

---

## 4.11 完整示例：页面结构可视化工具

### 页面查看器程序

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lmdb.h>

// 可视化页面布局
void visualize_page(MDB_page *mp, unsigned int psize) {
    unsigned int fill_pct;
    char bar[52];

    printf("\n=== Page %u Visualization ===\n", MP_PGNO(mp));
    printf("Flags: 0x%04x ", MP_FLAGS(mp));

    // 解析标志
    if (IS_META(mp)) printf("[META] ");
    if (IS_BRANCH(mp)) printf("[BRANCH] ");
    if (IS_LEAF(mp)) printf("[LEAF] ");
    if (IS_LEAF2(mp)) printf("[LEAF2] ");
    if (IS_OVERFLOW(mp)) printf("[OVERFLOW] ");
    if (mp->mp_flags & P_DIRTY) printf("[DIRTY] ");
    printf("\n");

    printf("Page Size: %u bytes\n", psize);
    printf("Header: %u bytes\n", PAGEHDRSZ);
    printf("Lower: %u\n", MP_LOWER(mp));
    printf("Upper: %u\n", MP_UPPER(mp));
    printf("Free: %u bytes\n", SIZELEFT(mp));
    printf("Keys: %u\n", NUMKEYS(mp));

    fill_pct = PAGEFILL(NULL, mp);
    printf("Fill: %u.%u%%\n", fill_pct / 10, fill_pct % 10);

    // 空间使用条
    int used = fill_pct / 20;
    memset(bar, '=', used);
    memset(bar + used, ' ', 50 - used);
    bar[50] = '\0';
    printf("[%s]\n", bar);

    // 列出节点
    printf("\nNodes:\n");
    unsigned int max_show = NUMKEYS(mp) > 10 ? 10 : NUMKEYS(mp);
    for (unsigned int i = 0; i < max_show; i++) {
        MDB_node *node = NODEPTR(mp, i);
        printf("  [%u] offset:%5u ", i, MP_PTRS(mp)[i]);
        printf(" key_sz:%u ", NODEKSZ(node));

        if (IS_LEAF2(mp)) {
            printf(" LEAF2");
        } else {
            printf(" data_sz:%u", NODEDSZ(node));
        }
        printf("\n");
    }
}

// 打印所有节点的键
void print_all_keys(MDB_env *env, MDB_dbi dbi) {
    MDB_txn *txn;
    MDB_cursor *cursor;
    MDB_val key, data;
    int rc;

    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    mdb_cursor_open(txn, dbi, &cursor);

    printf("\n=== All Keys in Database ===\n");
    rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    int count = 0;
    while (rc == 0) {
        printf("[%d] ", count++);
        printf("%.*s\n", (int)key.mv_size, (char *)key.mv_data);
        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
}

// 模拟并显示页面插入过程
void demonstrate_page_insert() {
    unsigned char buffer[4096];
    MDB_page *page = (MDB_page *)buffer;

    // 初始化为空页面
    memset(buffer, 0, 4096);
    page->mp_pgno = 1;
    page->mp_flags = P_LEAF;
    page->mp_lower = PAGEHDRSZ;
    page->mp_upper = 4096;

    printf("=== Page Insert Demonstration ===\n");
    printf("Initial: lower=%u upper=%u free=%u\n",
           PAGEHDRSZ, 4096, SIZELEFT(page));

    // 模拟插入不同大小的节点
    struct {
        char *key;
        unsigned int key_size;
        unsigned int data_size;
    } entries[] = {
        {"apple", 5, 10},
        {"banana", 6, 20},
        {"cherry", 6, 15},
        {"date", 4, 8},
        {"elderberry", 9, 25}
    };

    for (int i = 0; i < 5; i++) {
        unsigned int node_size = NODESIZE +
                              entries[i].key_size +
                              entries[i].data_size;

        printf("\nInserting '%s' (key=%u, data=%u, node=%u):\n",
               entries[i].key, entries[i].key_size,
               entries[i].data_size, node_size);

        if (node_size <= SIZELEFT(page)) {
            // 可以插入
            page->mp_upper -= node_size;
            page->mp_ptrs[i] = page->mp_upper;
            page->mp_lower += 2;

            printf("  ✓ Success: lower=%u upper=%u free=%u\n",
                   MP_LOWER(page), MP_UPPER(page), SIZELEFT(page));
        } else {
            printf("  ✗ Failed: need %u bytes, only %u available\n",
                   node_size, SIZELEFT(page));
            break;
        }
    }

    printf("\nFinal: %u keys inserted\n", NUMKEYS(page));
}

int main() {
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_stat stat;
    int rc;

    printf("=== LMDB Page Structure Viewer ===\n\n");

    // 打开环境
    rc = mdb_env_create(&env);
    rc = mdb_env_set_mapsize(env, 1024 * 1024 * 10);
    rc = mdb_env_open(env, "./testdb", 0, 0664);
    if (rc) {
        // 创建新数据库
        mdb_txn_begin(env, NULL, 0, &txn);
        mdb_dbi_open(txn, NULL, MDB_CREATE, &dbi);
        mdb_txn_commit(txn);
    }

    // 获取统计信息
    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    mdb_stat(txn, 1, &stat);
    mdb_txn_abort(txn);

    printf("Database Statistics:\n");
    printf("  Branch pages: %u\n", stat.ms_branch_pages);
    printf("  Leaf pages: %u\n", stat.ms_leaf_pages);
    printf("  Overflow pages: %u\n", stat.ms_overflow_pages);
    printf("  Entries: %zu\n", stat.ms_entries);

    // 显示所有键
    print_all_keys(env, dbi);

    // 演示页面插入
    demonstrate_page_insert();

    mdb_env_close(env);
    return 0;
}
```

编译运行：
```bash
gcc -o page_viewer page_viewer.c -llmdb
./page_viewer
```

---

## 4.12 页面分析工具

### 页面验证函数

```c
int validate_page(MDB_page *mp, unsigned int psize) {
    int errors = 0;

    printf("\n=== Validating Page %u ===\n", MP_PGNO(mp));

    // 检查页号
    if (MP_PGNO(mp) == 0 && !IS_META(mp)) {
        printf("[ERROR] Page 0 must be META\n");
        errors++;
    }

    // 检查标志
    uint16_t flags = MP_FLAGS(mp);
    uint16_t valid_flags = P_BRANCH | P_LEAF | P_OVERFLOW |
                              P_META | P_LEAF2 | P_SUBP;

    if (flags & ~valid_flags) {
        printf("[ERROR] Invalid flags: 0x%04x\n", flags & ~valid_flags);
        errors++;
    }

    // 检查页面类型互斥
    int type_count = !!IS_BRANCH(mp) + !!IS_LEAF(mp) +
                    !!IS_LEAF2(mp) + !!IS_OVERFLOW(mp);

    if (type_count != 1 && flags != 0) {
        printf("[ERROR] Multiple type flags\n");
        errors++;
    }

    // 检查边界
    if (MP_LOWER(mp) < PAGEHDRSZ) {
        printf("[ERROR] Lower bound invalid: %u\n", MP_LOWER(mp));
        errors++;
    }

    if (MP_UPPER(mp) > psize) {
        printf("[ERROR] Upper bound exceeds page size\n");
        errors++;
    }

    if (MP_LOWER(mp) > MP_UPPER(mp)) {
        printf("[ERROR] Lower > Upper\n");
        errors++;
    }

    if (errors == 0) {
        printf("✓ Page is valid\n");
    } else {
        printf("✗ Found %d error(s)\n", errors);
    }

    return errors;
}
```

---

## 4.13 今日练习

1. 为什么页面空间要向中间增长？
2. LEAF2 页面有什么优势和限制？
3. 如何判断一个页面是否需要分裂？
4. 溢出页链如何实现原子性更新？

---

## 明天预告

Day 5 将深入讲解 B+树实现。我们将学习：
- B+树的结构和遍历
- 节点搜索算法
- 树的平衡和分裂
- 键的比较函数

B+树是 LMDB 的核心数据结构，理解它才能理解整个系统！

---
**参考文献：**
- mdb.c: 1003-1220 (MDB_page, MDB_node)
- mdb.c: 1056-1092 (页面宏定义)
- mdb.c: 6046-6100 (mdb_node_search)
