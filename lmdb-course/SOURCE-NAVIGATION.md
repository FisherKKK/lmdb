# LMDB 源代码导航指南

本指南帮助你快速定位和理解 LMDB 源代码中的关键部分。

---

## 📂 源代码结构

```
libraries/liblmdb/
├── mdb.c          # 核心引擎（~11,000 行）
├── lmdb.h         # 公共 API 文档（~76,000 行含注释）
├── midl.c         # ID 列表实现
├── midl.h         # ID 列表头文件
├── mdb_*          # 工具程序
│   ├── mdb_stat.c
│   ├── mdb_copy.c
│   ├── mdb_dump.c
│   ├── mdb_load.c
│   └── mdb_drop.c
├── mtest*.c       # 测试程序
└── mplay.c        # 压力测试工具
```

---

## 🔍 核心数据结构位置

### MDB_env - 环境结构
**文件**: `mdb.c`
**位置**: 约 1309-1409 行
```c
typedef struct MDB_env {
    // 内存映射相关
    void       *me_map;       // 映射地址
    size_t      me_mapsize;   // 映射大小
    unsigned    me_psize;     // 页面大小

    // 文件相关
    int         me_fd;        // 数据文件描述符
    int         me_mfd;       // 元数据文件描述符
    int         me_lfd;       // 锁文件描述符

    // 锁和同步
    pthread_mutex_t me_mutex; // 线程互斥锁
    sem_t       *me_rmutex;   // 读锁（某些平台）

    // 事务管理
    MDB_txn     *me_txn;      // 当前写事务
    MDB_txninfo *me_txns;     // 事务信息

    // 空闲页面管理
    MDB_IDL      me_free_pgs;  // 空闲页面列表

    // 元数据
    MDB_meta    *me_metas[2];  // 两个元数据页
    MDB_page    *me_pbuf;      // 页面缓冲区

    // ... 更多字段
} MDB_env;
```

**关键使用场景**:
- 环境创建: `mdb_env_create()` → 约 4498 行
- 环境打开: `mdb_env_open()` → 约 4746 行
- 环境关闭: `mdb_env_close()` → 约 4831 行

---

### MDB_txn - 事务结构
**文件**: `mdb.c`
**位置**: 约 1222-1290 行
```c
struct MDB_txn {
    // 层次结构
    MDB_txn     *mt_parent;    // 父事务
    MDB_txn     *mt_child;     // 子事务

    // 基本信息
    txnid_t      mt_txnid;     // 事务 ID
    MDB_env     *mt_env;       // 所属环境

    // 页面管理
    pgno_t       mt_next_pgno;  // 下一页号
    MDB_IDL      mt_free_pgs;  // 释放的页面
    MDB_page    *mt_loose_pgs; // 松散页面
    int          mt_loose_count;

    // 脏页管理
    union {
        MDB_ID2L   dirty_list;   // 写事务：脏页列表
        MDB_reader *reader;      // 读事务：读者槽位
    } mt_u;

    // 数据库信息
    MDB_dbx      *mt_dbxs;      // 数据库辅助信息
    MDB_db       *mt_dbs;       // 数据库记录
    unsigned int *mt_dbiseqs;   // 数据库序列号

    // 状态
    MDB_dbi      mt_numdbs;     // 数据库数量
    unsigned int  mt_flags;      // 事务标志
} MDB_txn;
```

**关键函数**:
- `mdb_txn_begin()` → 约 3218 行
- `mdb_txn_commit()` → 约 3975 行
- `mdb_txn_abort()` → 约 3492 行

---

### MDB_page - 页面结构
**文件**: `mdb.c`
**位置**: 约 1168-1221 行
```c
typedef struct MDB_page {
    pgno_t    mp_pgno;      // 页号
    uint16_t  mp_pad;       // 填充
    uint16_t  mp_flags;     // 标志
    indx_t    mp_lower;     // 空闲下界
    indx_t    mp_upper;     // 空闲上界
    indx_t    mp_ptrs[0];   // 节点指针（动态）
} MDB_page;
```

**页面标志**:
```c
#define P_BRANCH       0x01  // 分支页
#define P_LEAF         0x02  // 叶子页
#define P_OVERFLOW     0x04  // 溢出页
#define P_META         0x08  // 元数据页
#define P_DIRTY        0x10  // 脏页（已修改）
#define P_LEAF2        0x20  // LEAF2 页（固定大小键）
#define P_SUBP         0x40  // 子页面
#define P_LOOSE        0x4000// 松散页
```

---

### MDB_node - 节点结构
**文件**: `mdb.c`
**位置**: 约 1093-1167 行
```c
typedef struct MDB_node {
    unsigned short mn_flags;  // 标志
    unsigned short mn_ksize;  // 键大小
    unsigned short mn_dsize;  // 数据大小
#define mn_pgno          mn_u.mw_pgno  // 页号（分支节点）
#define mn_datasync      mn_u.mw_datasync  // 数据异步
    unsigned short mn_datasync;
    // 键数据紧随其后
    char mn_data[1];
} MDB_node;
```

---

## 🔧 核心函数导航

### 事务管理

| 函数 | 位置 | 功能 |
|------|------|------|
| `mdb_txn_begin()` | 3218-3310 | 开始新事务 |
| `mdb_txn_commit()` | 3975-4210 | 提交事务 |
| `mdb_txn_abort()` | 3492-3510 | 中止事务 |
| `mdb_txn_renew0()` | 3061-3180 | 续期事务 |
| `mdb_cursor_open()` | 8765-8785 | 打开游标 |
| `mdb_cursor_close()` | 8787-8810 | 关闭游标 |

### 页面操作

| 函数 | 位置 | 功能 |
|------|------|------|
| `mdb_page_malloc()` | 约 2480 | 分配页面 |
| `mdb_page_free()` | 约 2520 | 释放页面 |
| `mdb_page_touch()` | 约 2310 | 触摸页面（写时复制） |
| `mdb_page_split()` | 约 5570 | 分裂页面 |
| `mdb_page_search()` | 6666-6750 | 搜索页面 |
| `mdb_node_search()` | 6046-6134 | 搜索节点 |

### 数据操作

| 函数 | 位置 | 功能 |
|------|------|------|
| `mdb_put()` | 约 5700 | 插入/更新数据 |
| `mdb_get()` | 约 5500 | 获取数据 |
| `mdb_del()` | 约 5800 | 删除数据 |
| `mdb_cursor_put()` | 约 7425 | 游标插入 |
| `mdb_cursor_get()` | 约 7470 | 游标获取 |
| `mdb_cursor_del()` | 约 7530 | 游标删除 |

### 辅助函数

| 函数 | 位置 | 功能 |
|------|------|------|
| `mdb_cmp_memn()` | 约 6500 | 内存比较 |
| `mdb_cmp_int()` | 约 6470 | 整数比较 |
| `mdb_cmp_long()` | 约 6480 | 长整数比较 |
| `mdb_midl_append()` | midl.c | ID列表追加 |
| `mdb_midl_search()` | midl.c | ID列表搜索 |

---

## 🗺️ 按功能浏览源代码

### 1. 内存映射相关

```
mdb.c 1-500 行：
  - 平台兼容性宏
  - 内存对齐宏
  - 原子操作宏

mdb.c 4540-4650 行：
  - mdb_env_map() - 映射数据库
  - mdb_env_map_resize() - 调整映射大小

mdb.c 4831-4900 行：
  - mdb_env_close() - 关闭环境
  - mdb_env_cwalk() - 清理映射
```

### 2. B+树操作

```
mdb.c 6046-6134 行：
  - mdb_node_search() - 二分查找

mdb.c 6666-6750 行：
  - mdb_page_search() - 页面搜索

mdb.c 5570-5680 行：
  - mdb_page_split() - 页面分裂

mdb.c 5520-5560 行：
  - mdb_page_merge() - 页面合并
```

### 3. 事务管理

```
mdb.c 3061-3180 行：
  - mdb_txn_renew0() - 事务续期

mdb.c 3218-3310 行：
  - mdb_txn_begin() - 开始事务

mdb.c 3492-3510 行：
  - mdb_txn_abort() - 中止事务

mdb.c 3975-4210 行：
  - _mdb_txn_commit() - 提交事务
  - mdb_page_flush() - 刷新脏页
```

### 4. MVCC 和版本管理

```
mdb.c 830-850 行：
  - MDB_reader 结构
  - 读者表管理

mdb.c 3061-3100 行：
  - 读事务注册
  - 读者表槽位分配

mdb.c 3090-3120 行：
  - 获取读锁

mdb.c 3140-3160 行：
  - 获取写锁
```

---

## 🎯 阅读顺序建议

### 入门级（第一次阅读）

```
1. lmdb.h (前 1000 行)
   - API 概览
   - 数据结构定义
   - 使用示例

2. mdb.c (1-500 行)
   - 宏定义
   - 基础类型

3. mdb.c (mtest*.c 测试程序)
   - 简单示例
   - 基本用法
```

### 进阶级（理解核心）

```
1. mdb.c (1168-1221 行)
   - MDB_page 结构
   - 页面布局

2. mdb.c (1093-1167 行)
   - MDB_node 结构
   - 节点格式

3. mdb.c (6046-6134 行)
   - 搜索算法
   - 二分查找

4. mdb.c (3218-3310 行)
   - 事务开始
   - 读 vs 写事务
```

### 高阶级（深入研究）

```
1. mdb.c (3975-4210 行)
   - 事务提交
   - 页面刷新
   - 元数据更新

2. mdb.c (5570-5680 行)
   - 页面分裂
   - 树的平衡

3. mdb.c (400-500, 8000+ 行)
   - 平台特定代码
   - 优化技巧

4. midl.c/midl.h
   - ID 列表实现
   - 高效数据结构
```

---

## 📊 重要宏定义

### 页面操作宏
**位置**: mdb.c 400-600 行
```c
// 页面大小
#define MDB_MINKEYS     2
#define MDB_MAXKEYSIZE   511

// 页面计算
#define PAGEHDRSZ   (sizeof(MDB_page))
#define METADATA(p) ((void *)((char *)(p) + PAGEHDRSZ))

// 节点访问
#define NODEKEY       NODEKEY
#define NODEDATA      NODEDATA
#define NODEPGNO      NODEPGNO
#define NODEDSZ       NODEDSZ
```

### 调试宏
**位置**: mdb.c 100-200 行
```c
#ifdef MDB_DEBUG
    // 调试输出
    #define DPRINTF(x) if (mdb_env_debug) fprintf x
#else
    #define DPRINTF(x) ((void)0)
#endif

// 断言
#define assert(x) ((void)0)
```

---

## 🔬 调试技巧

### 使用 GDB 调试

```bash
# 编译调试版本
cd libraries/liblmdb
make clean
make CFLAGS="-g -O0"

# 运行 GDB
gdb ./mtest

# 有用的断点
(gdb) break mdb_txn_begin
(gdb) break mdb_node_search
(gdb) break mdb_page_split

# 查看变量
(gdb) print *mp
(gdb) print *node
(gdb) print txn->mt_txnid
(gdb) print env->me_mapsize
```

### 使用静态分析

```bash
# 使用 cscope
cd libraries/liblmdb
cscope -b
cscope -d  # 交互式查询

# 查找函数定义
# 查找函数调用
# 查找全局定义
```

### 使用代码浏览工具

```bash
# 使用 ctags
ctags -R .
vim -t mdb_txn_begin  # 跳转到定义

# 使用 VSCode
# 1. 安装 C/C++ 扩展
# 2. 打开 mdb.c
# 3. 使用符号导航
```

---

## 📝 代码注释指南

LMDB 源代码的注释风格：

### 函数注释
```c
/** @brief 函数简要说明
 *
 * @param[in] env 环境句柄
 * @param[in] txn 事务句柄
 * @return 返回值说明
 *
 * 详细说明...
 */
static int function_name(MDB_env *env, MDB_txn *txn) {
    // 实现
}
```

### 变量注释
```c
unsigned int mt_flags;    // 事务标志
// 或
unsigned int mt_flags;    /**< 事务标志 */
```

---

## 🚀 快速查找技巧

### 在 vim 中浏览
```vim
:tag mdb_txn_begin
:ts select /^mdb_/
:vsp | split  # 分屏浏览
:Explore mdb.c  # 使用文件浏览器
```

### 使用 grep
```bash
# 查找函数定义
grep -n "^mdb_txn_begin" mdb.c

# 查找结构体定义
grep -n "typedef struct MDB_" mdb.c

# 查找宏定义
grep -n "#define MDB_" mdb.c | head -20
```

### 使用 cscope
```bash
# 生成索引
cscope -b

# 查找函数调用
cscope -d

# 查找符号定义
cscope -7

# 查找包含文件
cscope -8
```

---

## 📚 相关文件索引

### API 文档
- **lmdb.h**: 完整 API 文档（含详细注释）
- **mdb.c**: 核心实现（代码即文档）

### 测试程序
- **mtest.c**: 基本功能测试
- **mtest2.c**: 多进程测试
- **mtest3.c**: 并发测试
- **mtest4.c**: 大数据测试
- **mtest5.c**: 压力测试
- **mtest6.c**: 复杂事务测试

### 工具程序
- **mdb_stat.c**: 数据库统计
- **mdb_copy.c**: 数据库复制
- **mdb_dump.c**: 数据库转储
- **mdb_load.c**: 数据库加载
- **mdb_drop.c**: 数据库删除

---

## 💡 阅读建议

### 第一步：概览
1. 阅读 lmdb.h 的前 1000 行
2. 理解 API 设计和数据结构
3. 运行 mtest 看实际效果

### 第二步：核心流程
1. 跟踪一个简单的 put 操作
2. 跟踪一个 get 操作
3. 理解事务提交过程

### 第三步：深入细节
1. 研究页面分裂算法
2. 理解 MVCC 实现
3. 分析锁机制

### 第四步：专项研究
1. 平台特定代码（Windows/Linux）
2. 性能优化技巧
3. 错误处理机制

---

## 🎓 学习检查清单

通过以下检查清单验证你的理解：

### 基础理解
- [ ] 能解释 MDB_env 的主要字段
- [ ] 能解释 MDB_txn 的主要字段
- [ ] 能解释 MDB_page 的布局
- [ ] 能解释 MDB_node 的格式

### 核心流程
- [ ] 能追踪 put 操作的完整流程
- [ ] 能追踪 get 操作的完整流程
- [ ] 能追踪事务提交的完整流程
- [ ] 能理解 B+树搜索算法

### 高级主题
- [ ] 理解 MVCC 的实现机制
- [ ] 理解写时复制的实现
- [ ] 理解页面分裂的算法
- [ ] 理解锁管理的实现

---

**提示**: 使用本指南配合 README.md 和 CHEATSHEET.md，形成完整的学习体系！

开始学习：[Day 1 - LMDB 概述](Day-01-LMDB-Overview.md)
