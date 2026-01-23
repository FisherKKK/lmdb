# LMDB 底层实现 14天课程 - Day 2

## 内存映射 I/O (Memory-Mapped I/O) - LMDB 的基石

欢迎回来！今天我们将深入探讨 LMDB 高性能的根本原因 - 内存映射 I/O。理解 mmap 的工作原理是掌握 LMDB 的关键。

---

## 今天的目标

1. 理解内存映射 I/O 的原理
2. 掌握 LMDB 如何使用 mmap
3. 理解页面管理和地址空间
4. 了解不同平台的 mmap 实现

---

## 2.1 什么是内存映射 I/O？

### 传统文件 I/O vs 内存映射 I/O

```
传统文件 I/O (read/write):
┌─────────┐    read()    ┌──────────┐    数据拷贝    ┌─────────┐
│ 应用程序 │ ──────────> │  内核    │ ──────────> │  应用   │
│ 缓冲区   │ <────────── │  缓冲区   │ <────────── │  缓冲区  │
└─────────┘    write()   └──────────┘    数据拷贝    └─────────┘
                            │
                            ▼
                       ┌─────────┐
                       │ 磁盘    │
                       └─────────┘

内存映射 I/O (mmap):
┌─────────────────────────────────────────────┐
│          应用程序虚拟地址空间                │
│  ┌─────────────────────────────────────┐   │
│  │    映射区域 (直接访问文件)           │   │
│  └─────────────────────────────────────┘   │
└─────────────────────────────────────────────┘
                    │ 自动同步
                    ▼
              ┌─────────┐
              │  磁盘   │
              └─────────┘
```

### mmap 的优势

| 特性 | 传统 I/O | mmap |
|------|----------|------|
| 数据拷贝 | 2次（内核→用户，用户→内核） | 0次 |
| 系统调用 | 每次 read/write | 一次 mmap |
| 缓存管理 | 应用/内核双重缓存 | 操作系统统一管理 |
| 代码复杂度 | 需要缓冲区管理 | 直接访问 |

---

## 2.2 LMDB 中的 mmap 实现

### mdb_env_map() - 核心映射函数

让我们看看 LMDB 如何执行内存映射（mdb.c:4540）：

```c
static int ESECT
mdb_env_map(MDB_env *env, void *addr)
{
    MDB_page *p;
    unsigned int flags = env->me_flags;

#ifdef _WIN32
    // Windows 实现：使用 NT API
    HANDLE mh;
    void *map;
    SIZE_T msize;

    // 创建文件映射节
    rc = NtCreateSection(&mh, access, NULL, NULL, secprot, SEC_RESERVE, env->me_fd);
    if (rc)
        return mdb_nt2win32(rc);

    // 映射视图到地址空间
    rc = NtMapViewOfSection(mh, GetCurrentProcess(), &map, 0, 0, NULL, &msize,
                           ViewUnmap, alloctype, pageprot);
    NtClose(mh);
    if (rc)
        return mdb_nt2win32(rc);
    env->me_map = map;

#else
    // Unix 实现：使用 POSIX mmap
    int mmap_flags = MAP_SHARED;
    int prot = PROT_READ;

    // 设置写权限
    if (flags & MDB_WRITEMAP) {
        prot |= PROT_WRITE;
        if (ftruncate(env->me_fd, env->me_mapsize) < 0)
            return ErrCode();
    }

    // 执行内存映射
    env->me_map = mmap(addr, env->me_mapsize, prot, mmap_flags,
                       env->me_fd, 0);
    if (env->me_map == MAP_FAILED) {
        env->me_map = NULL;
        return ErrCode();
    }

    // 关闭读预读（对大数据库有益）
    if (flags & MDB_NORDAHEAD) {
#ifdef MADV_RANDOM
        madvise(env->me_map, env->me_mapsize, MADV_RANDOM);
#endif
    }
#endif

    // 设置元数据页指针
    p = (MDB_page *)env->me_map;
    env->me_metas[0] = METADATA(p);
    env->me_metas[1] = (MDB_meta *)((char *)env->me_metas[0] + env->me_psize);

    return MDB_SUCCESS;
}
```

### 代码解读

1. **平台分支**：Windows 和 Unix 使用不同的 API
2. **权限控制**：`PROT_READ` 和 `PROT_WRITE` 控制访问权限
3. **MAP_SHARED**：映射对其他进程可见，修改会写回文件
4. **元数据页**：页 0 和页 1 是元数据页，`me_metas[0/1]` 指向它们

---

## 2.3 mmap 标志与 LMDB 配置

### 保护标志 (prot)

```c
int prot = PROT_READ;      // 只读映射（默认）

if (flags & MDB_WRITEMAP) {
    prot |= PROT_WRITE;    // 添加写权限
}
```

**MDB_WRITEMAP** 的权衡：
- ✅ 更高的写入性能
- ❌ 失去了只读模式的安全性
- ❌ 应用程序的野指针可能破坏数据库

### 映射标志 (mmap_flags)

```c
int mmap_flags = MAP_SHARED;  // 共享映射（必需）

#ifdef MAP_NOSYNC  /* FreeBSD */
if (flags & MDB_NOSYNC)
    mmap_flags |= MAP_NOSYNC;  // 禁用自动同步
#endif
```

**MAP_SHARED** 的作用：
- 映射对其他进程可见
- 修改会写回文件
- 实现进程间共享

### MADV_RANDOM - 性能优化

```c
if (flags & MDB_NORDAHEAD) {
    madvise(env->me_map, env->me_mapsize, MADV_RANDOM);
}
```

- 对于比 RAM 大的数据库，关闭预读可以避免缓存污染
- 默认行为是顺序访问预读，这对随机访问不利

---

## 2.4 元数据页的布局

```c
p = (MDB_page *)env->me_map;
env->me_metas[0] = METADATA(p);
env->me_metas[1] = (MDB_meta *)((char *)env->me_metas[0] + env->me_psize);
```

### 数据库文件布局

```
┌─────────────────────────────────────────────────────────┐
│  页 0: Meta Page 0                                      │
│  - mm_magic: MDB_MAGIC                                 │
│  - mm_version: MDB_DATA_VERSION                        │
│  - mm_address: 映射地址                                 │
│  - mm_mapsize: 映射大小                                 │
│  - mm_dbs[0]: Free DB                                  │
│  - mm_dbs[1]: Main DB                                  │
│  - mm_last_pg: 最后使用的页号                           │
│  - mm_txnid: 提交此页的事务ID                          │
├─────────────────────────────────────────────────────────┤
│  页 1: Meta Page 1                                     │
│  (与页 0 结构相同，两个元数据页交替使用)                  │
├─────────────────────────────────────────────────────────┤
│  页 2+: B+树节点和用户数据                              │
└─────────────────────────────────────────────────────────┘
```

### 为什么有两个元数据页？

**原子性更新机制**：
- 事务 N 写元数据页 N % 2
- 元数据页更新是原子的（`mm_txnid` 校验）
- 读取时选择 `mm_txnid` 较大的元数据页
- 崩溃后可以恢复到最近的完整状态

```c
// 选择较新的元数据页
static MDB_meta *mdb_env_pick_meta(MDB_env *env) {
    MDB_meta *meta0 = env->me_metas[0];
    MDB_meta *meta1 = env->me_metas[1];
    // 选择事务ID较大的
    return (meta0->mm_txnid > meta1->mm_txnid) ? meta0 : meta1;
}
```

---

## 2.5 地址空间管理

### MDB_VL32 - 大地址空间模式

在 32 位系统上，地址空间有限。LMDB 提供了 MDB_VL32 模式：

```c
#ifdef MDB_VL32
    // 只映射元数据页
    env->me_map = mmap(addr, NUM_METAS * env->me_psize,
                       prot, mmap_flags, env->me_fd, 0);

    // 其他页面按需动态映射
    // 使用 mt_rpages 列表跟踪已映射的页面块
#endif
```

### 动态映射（MDB_VL32）

```c
#define MDB_RPAGE_CHUNK  16   // 每次映射 16 个页面
#define MDB_TRPAGE_SIZE  4096 // rpages 数组大小

struct MDB_txn {
    // ...
    MDB_ID3L  mt_rpages;  // 已映射的页面块列表
    unsigned int mt_rpcheck; // 回收阈值
};
```

**工作原理**：
1. 访问页面时检查是否已映射
2. 未映射则通过 mmap 映射所需块
3. 维护一个 LRU 列表，必要时释放旧映射

---

## 2.6 平台特定实现

### Windows 实现

```c
// Windows 使用 NT 原生 API（而非 Win32 API）
// 原因：可以实现增量文件增长

typedef NTSTATUS (WINAPI NtCreateSectionFunc)
  (OUT PHANDLE sh, IN ACCESS_MASK acc,
   IN void *oa OPTIONAL,
   IN PLARGE_INTEGER ms OPTIONAL,
   IN ULONG pp, IN ULONG aa, IN HANDLE fh OPTIONAL);

typedef NTSTATUS (WINAPI NtMapViewOfSectionFunc)
  (IN PHANDLE sh, IN HANDLE ph,
   IN OUT PVOID *addr, IN ULONG_PTR zbits,
   IN SIZE_T cs, IN OUT PLARGE_INTEGER off OPTIONAL,
   IN OUT PSIZE_T vs, IN SECTION_INHERIT ih,
   IN ULONG at, IN ULONG pp);
```

**优势**：
- `SEC_RESERVE` 允许保留地址空间但不立即分配物理存储
- 增量文件增长，避免一次性占用大量磁盘空间

### Unix 实现

```c
// Unix 使用标准 POSIX API
env->me_map = mmap(addr, env->me_mapsize, prot, mmap_flags,
                   env->me_fd, 0);
```

**mmap 参数**：
- `addr`：建议的映射地址（NULL 表示由系统选择）
- `env->me_mapsize`：映射大小（可能达到数 TB）
- `prot`：保护标志（PROT_READ | PROT_WRITE）
- `mmap_flags`：映射标志（MAP_SHARED）
- `env->me_fd`：文件描述符
- `0`：文件偏移量

---

## 2.7 页面大小探测

```c
// mdb.c 中获取系统页面大小
#ifndef _WIN32
    size_t size = sysconf(_SC_PAGESIZE);
#else
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    size = si.dwPageSize;
#endif
```

**常见页面大小**：
- x86/x86_64: 4096 字节
- ARM: 4096 字节
- 某些 Unix: 可能是 8192 或更大

LMDB 在打开环境时检测页面大小，并存储在元数据页的 `mm_psize` 字段中。

---

## 2.8 零拷贝读取的原理

```
传统方式：
文件 → 内核缓冲区 → read() → 用户缓冲区 → 应用程序
      (拷贝1)                    (拷贝2)

LMDB 方式：
文件 → mmap 映射 → 直接访问
      (操作系统的页面缓存)
```

### 代码示例

```c
// 传统方式
char buf[1024];
read(fd, buf, 1024);      // 拷贝到 buf
process(buf);             // 处理数据

// LMDB 方式
MDB_val data;
mdb_get(txn, dbi, &key, &data);
// data.mv_data 直接指向映射内存，无需拷贝
process(data.mv_data);
```

### MDB_val 结构

```c
typedef struct MDB_val {
    size_t  mv_size;    // 数据大小
    void    *mv_data;   // 指向映射内存的指针
} MDB_val;
```

**关键点**：`mv_data` 指向的是映射到内存的文件内容，不是分配的缓冲区！

---

## 2.9 mmap 的代价与权衡

### 代价

1. **地址空间占用**：映射区域占用虚拟地址空间
2. **页面错误**：首次访问时触发页面错误
3. **复杂性**：调试更困难（指针可能指向映射文件）

### 权衡

| 模式 | 优点 | 缺点 |
|------|------|------|
| 只读映射 (默认) | 安全，防止意外修改 | 写入需要额外的拷贝 |
| 写映射 (WRITEMAP) | 更高写入性能 | 风险：野指针可能损坏数据 |

---

## 2.10 完整示例：探索 mmap 行为

### 示例：观察 LMDB 的内存映射

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <lmdb.h>

void print_memory_info(MDB_env *env) {
    printf("=== Memory Mapping Information ===\n");
    printf("Map address: %p\n", env->me_map);
    printf("Map size: %zu bytes (%.2f MB)\n",
           env->me_mapsize,
           (double)env->me_mapsize / (1024 * 1024));
    printf("Page size: %u bytes\n", env->me_psize);
    printf("Max pages: %u\n", env->me_maxpg);
    printf("Number of pages: %zu\n",
           env->me_mapsize / env->me_psize);

    // 计算元数据页位置
    MDB_meta *meta0 = env->me_metas[0];
    MDB_meta *meta1 = env->me_metas[1];

    printf("\n=== Meta Pages ===\n");
    printf("Meta0 address: %p\n", meta0);
    printf("Meta0 txnid: %llu\n", (unsigned long long)meta0->mm_txnid);
    printf("Meta1 address: %p\n", meta1);
    printf("Meta1 txnid: %llu\n", (unsigned long long)meta1->mm_txnid);

    // 显示当前选择哪个元数据页
    MDB_meta *active = (meta0->mm_txnid > meta1->mm_txnid) ? meta0 : meta1;
    printf("\nActive meta page: %s (txnid=%llu)\n",
           (active == meta0) ? "Meta0" : "Meta1",
           (unsigned long long)active->mm_txnid);
}

int main() {
    MDB_env *env;
    int rc;

    // 创建环境
    rc = mdb_env_create(&env);
    if (rc) {
        fprintf(stderr, "mdb_env_create: %s\n", mdb_strerror(rc));
        return 1;
    }

    // 设置不同的映射大小测试
    mdb_env_set_mapsize(env, 1024 * 1024 * 10);  // 10MB

    printf("Opening LMDB environment...\n");
    rc = mdb_env_open(env, "./testdb", 0, 0664);
    if (rc) {
        fprintf(stderr, "mdb_env_open: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    print_memory_info(env);

    // 获取进程 ID
    printf("\n=== Process Information ===\n");
    printf("Process ID: %d\n", getpid());
    printf("Check memory map with: cat /proc/%d/maps\n", getpid());

    // 等待用户输入
    printf("\nPress Enter to view process maps...\n");
    getchar();

    // 使用 system 调用显示映射
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "cat /proc/%d/maps | grep testdb", getpid());
    system(cmd);

    mdb_env_close(env);
    return 0;
}
```

编译运行：
```bash
gcc -o mmap_explorer mmap_explorer.c -llmdb
./mmap_explorer
```

---

## 2.11 实践：调试 mmap 行为

### 查看进程的内存映射

```bash
# Linux
pmap <pid>
cat /proc/<pid>/maps

# macOS
vmmap <pid>

# 观察输出
# 会有类似这样的行：
# 7f1234567000-7f1298765000 rw-s 00000000 08:01 123456 /path/to/data.mdb
```

### 使用 strace 观察系统调用

```bash
strace -e trace=mmap,munmap,madvise ./mtest
```

### 比较不同映射模式的性能

```c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <lmdb.h>

// 性能测试函数
double benchmark_mode(int flags, const char *mode_name) {
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;
    int rc;
    struct timespec start, end;

    // 创建环境
    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024 * 100);
    mdb_env_open(env, mode_name, flags, 0664);

    clock_gettime(CLOCK_MONOTONIC, &start);

    // 写入测试
    const int COUNT = 100000;
    for (int i = 0; i < COUNT; i++) {
        mdb_txn_begin(env, NULL, 0, &txn);
        mdb_dbi_open(txn, NULL, 0, &dbi);

        char buf[64];
        snprintf(buf, sizeof(buf), "key_%d", i);
        key.mv_data = buf;
        key.mv_size = strlen(buf);
        data.mv_data = "test_data_value_here";
        data.mv_size = strlen(data.mv_data);

        mdb_put(txn, dbi, &key, &data, 0);
        mdb_txn_commit(txn);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec - start.tv_sec) +
                    (end.tv_nsec - start.tv_nsec) / 1e9;

    mdb_env_close(env);

    printf("%s: %.3f seconds (%.0f ops/sec)\n",
           mode_name, elapsed, COUNT / elapsed);

    return elapsed;
}

int main() {
    printf("=== LMDB mmap Mode Performance Comparison ===\n\n");

    printf("Testing default mode (read-only mmap)...\n");
    double t1 = benchmark_mode(0, "./testdb_default");

    printf("\nTesting WRITEMAP mode...\n");
    double t2 = benchmark_mode(MDB_WRITEMAP, "./testdb_writemap");

    printf("\n=== Results ===\n");
    printf("WRITEMAP is %.2fx faster\n", t1 / t2);

    return 0;
}
```

---

## 2.12 今日练习

### 练习 1: 运行 mmap 探索器
编译并运行上面的 `mmap_explorer.c` 程序，观察：
- 映射的地址
- 映射的大小
- 元数据页的位置和事务ID

### 练习 2: 运行性能比较
编译并运行性能比较程序，比较：
- 默认模式 vs WRITEMAP 模式的性能差异
- 测量不同数据量下的性能

### 练习 3: 观察系统调用
使用 strace 观察 mmap 相关的系统调用：
```bash
strace -e trace=mmap,munmap,madvise,mprotect,pwrite,pwritev ./mtest 2>&1 | less
```

### 练习 4: 代码阅读
在 mdb.c 中找到并阅读：
1. `mdb_env_map()` - 行 4540
2. `mdb_env_set_mapsize()` - 行 4651
3. `mdb_env_pick_meta()` - 选择较新的元数据页

---

## 2.13 常见问题解答

### Q1: mmap 的最大映射大小有限制吗？

**A:** 是的，有以下限制：
- 32位系统：虚拟地址空间约 4GB
- 64位系统：理论上限很大，但受限于：
  - 可用磁盘空间
  - 文件系统限制
  - 地址空间布局

### Q2: LMDB 的映射大小可以动态调整吗？

**A:** 可以，但有条件：
- 必须在没有活跃事务时调整
- 只能增大，不能缩小（除非关闭环境）
- 使用 `mdb_env_set_mapsize()` 设置新大小

### Q3: 为什么需要设置映射大小？

**A:** 因为：
- mmap 需要预留虚拟地址空间
- 文件可能增长，需要空间
- 预分配比重新映射更高效

### Q4: MAP_SHARED vs MAP_PRIVATE？

**A:** 区别在于：
- `MAP_SHARED`：修改写回文件，LMDB 使用
- `MAP_PRIVATE`：写时复制（COW），修改不写回

---

## 2.14 延伸阅读

- [Linux mmap 手册](https://man7.org/linux/man-pages/man2/mmap.2.html)
- [madvise 手册](https://man7.org/linux/man-pages/man2/madvise.2.html)
- [Windows 内存映射文件](https://docs.microsoft.com/en-us/windows/win32/memory/memory-mapped-files)

---

## 2.15 思考题

1. 为什么 LMDB 选择 mmap 而不是直接的 read/write？
2. MDB_WRITEMAP 的安全性问题是什么？何时应该使用它？
3. 为什么有两个元数据页？这个设计如何保证原子性？
4. 在 32 位系统上，MDB_VL32 是如何解决地址空间限制的？
5. MAP_SHARED 和 MAP_PRIVATE 的主要区别是什么？

---

## 明天预告

Day 3 将深入讲解 `MDB_env` - 数据库环境。我们将学习：
- 环境的创建和初始化
- 环境的配置选项
- 锁文件的结构
- 读者/写入者协调机制

准备好了吗？明天我们将揭开 LMDB 环境管理的神秘面纱！

---
**参考文献：**
- mdb.c: 4540-4650 (mdb_env_map, mdb_env_set_mapsize)
- mdb.c: 1000-1100 (MDB_page, MDB_meta 结构)
- lmdb.h: mdb_env_open 的文档
