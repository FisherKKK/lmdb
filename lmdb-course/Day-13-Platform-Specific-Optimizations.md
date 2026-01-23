# LMDB 底层实现 14天课程 - Day 13

## 平台特定优化 - 跨平台的艺术

欢迎回来！今天我们将深入 LMDB 如何针对不同平台进行优化。LMDB 支持多种操作系统和硬件架构，每个平台都有其独特之处。

---

## 今天的目标

1. 理解 Windows 特定实现
2. 掌握 Unix 变体的差异
3. 学习硬件架构的特殊处理
4. 了解性能优化技巧

---

## 13.1 Windows 特定实现

### NT 原生 API 的使用

```c
// Windows 不使用标准 Win32 API
// 而是使用 NT 原生 API

// 为什么？
// 1. 支持增量文件增长
// 2. 更好的性能
// 3. 更细粒度的控制

// NtCreateSection - 创建文件映射节
typedef NTSTATUS (WINAPI NtCreateSectionFunc)(
    OUT PHANDLE sh,
    IN ACCESS_MASK acc,
    IN void *oa OPTIONAL,
    IN PLARGE_INTEGER ms OPTIONAL,
    IN ULONG pp,
    IN ULONG aa,
    IN HANDLE fh OPTIONAL
);

// NtMapViewOfSection - 映射视图到地址空间
typedef NTSTATUS (WINAPI NtMapViewOfSectionFunc)(
    IN PHANDLE sh,
    IN HANDLE ph,
    IN OUT PVOID *addr,
    IN ULONG_PTR zbits,
    IN SIZE_T cs,
    IN OUT PLARGE_INTEGER off OPTIONAL,
    IN OUT PSIZE_T vs,
    IN SECTION_INHERIT ih,
    IN ULONG at,
    IN ULONG pp
);
```

### 增量文件增长

```c
// Windows 支持两种文件增长模式

// 模式 1: 预分配整个文件 (MDB_FIXEDSIZE)
// 编译时定义
#ifdef MDB_FIXEDSIZE
    LARGE_INTEGER fsize;
    fsize.LowPart = msize & 0xffffffff;
    fsize.HighPart = msize >> 16 >> 16;
    rc = NtCreateSection(&mh, access, NULL, &fsize,
                        secprot, SEC_RESERVE, env->me_fd);
#endif

// 模式 2: 增量增长（默认）
// 文件大小随需要增长
rc = NtCreateSection(&mh, access, NULL, NULL,
                    secprot, SEC_RESERVE, env->me_fd);

// SEC_RESERVE 标志的作用：
// • 保留地址空间
// • 不立即分配物理存储
// • 按需增长
```

### Windows 锁机制

```c
// Windows 使用命名的互斥量

char me_mutexname[sizeof(MUTEXNAME_PREFIX) + 11];

// 构造互斥量名称
sprintf(me_mutexname, "%s%u", MUTEXNAME_PREFIX, env->me_pid);

// 创建/打开互斥量
HANDLE mutex = CreateMutex(NULL, FALSE, me_mutexname);

// 等待
WaitForSingleObject(mutex, INFINITE);

// 释放
ReleaseMutex(mutex);
```

### Windows 文件 I/O

```c
// 使用异步 I/O 提高性能

typedef struct {
    OVERLAPPED ov;
    // 其他字段
} OVERLAPPED;

// 异步写入
WriteFile(env->me_ovfd, buf, size, &written, &ov);

// 等待完成
GetOverlappedResult(env->me_ovfd, &ov, &written, TRUE);
```

---

## 13.2 Unix 变体

### Linux

```c
// Linux 特性：

// 1. fdatasync 问题
#ifdef __linux__
// fdatasync 在 ext3/ext4 的旧内核上有问题
#define BROKEN_FDATASYNC
// 可以通过定义 MDB_FDATASYNC_WORKS 来启用
#endif

// 2. MADV_RANDOM - 关闭预读
#ifdef MADV_RANDOM
madvise(env->me_map, env->me_mapsize, MADV_RANDOM);
#endif

// 3. Robust Mutex - 健壮互斥量
#ifdef MDB_USE_ROBUST
pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
#endif
```

### macOS

```c
// macOS 特性：

// 1. fcntl F_FULLFSYNC 代替 fsync
#ifdef __APPLE__
#define MDB_FDATASYNC(fd) fcntl(fd, F_FULLFSYNC)
#endif

// 2. System V 信号量（旧版本）
#if defined(__APPLE__) && !defined(MDB_USE_POSIX_MUTEX)
#define MDB_USE_SYSV_SEM 1
#endif

// 3. 缓存刷新
#if defined(__mips) && defined(__linux)
cacheflush(addr, bytes, cache);
#endif
```

### FreeBSD

```c
// FreeBSD 特性：

// 1. MAP_NOSYNC - 不同步映射
#ifdef MAP_NOSYNC
if (flags & MDB_NOSYNC)
    mmap_flags |= MAP_NOSYNC;
#endif

// 2. POSIX 信号量
#if defined(__FreeBSD__) && __FreeBSD_version >= 1100110
#define MDB_USE_POSIX_MUTEX 1
#define MDB_USE_ROBUST 1
#endif
```

### BSD 变体

```c
// NetBSD, OpenBSD 等

// 1. 缺少 union semun 定义
#if defined(__NetBSD__) && !defined(_SEM_SEMUN_UNDEFINED)
#define _SEM_SEMUN_UNDEFINED 1
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};
#endif

// 2. System V 信号量
#if defined(MDB_USE_SYSV_SEM)
// 使用 semget/semop
#endif
```

---

## 13.3 硬件架构特定处理

### MIPS 架构

```c
// MIPS 有缓存一致性问题

#if defined(__mips) && defined(__linux__)
#include <sys/cachectl.h>

// 需要显式刷新缓存
#define CACHEFLUSH(addr, bytes, cache) cacheflush(addr, bytes, cache)
#else
#define CACHEFLUSH(addr, bytes, cache)
#endif

// 为什么需要？
// MIPS 的数据缓存和指令缓存是独立的
// 修改代码后需要刷新缓存
```

### ARM 架构

```c
// ARM 的特殊考虑

// 1. 内存对齐
// ARM 对未对齐访问的处理因版本而异

// 2. 内存屏障
// 可能需要内存屏障指令

// 3. 大小端
// ARM 支持两种模式
#if BYTE_ORDER == BIG_ENDIAN
// 大端序处理
#else
// 小端序处理
#endif
```

### x86/x86_64

```c
// x86 特性

// 1. 未对齐访问 OK
#define MISALIGNED_OK 1

// 2. 可以直接复制页号
#define COPY_PGNO(dst, src) dst = src

// 3. 使用快速的内存操作
// memcpy, memset 等高度优化
```

---

## 13.4 同步机制差异

### fdatasync vs fsync

```c
// Linux fdatasync 问题

// 在旧内核上，ext3/ext4 的 fdatasync 不可靠
// 可能只同步元数据而不同步数据

// LMDB 的解决方案：
#ifdef BROKEN_FDATASYNC
// 使用 fsync 代替 fdatasync
#define MDB_FSYNCONLY 1  // 只用 fsync，不用 fdatasync
#endif

// macOS 解决方案：
#ifdef __APPLE__
// 使用 F_FULLFSYNC
#define MDB_FDATASYNC(fd) fcntl(fd, F_FULLFSYNC)
#endif
```

### msync 用法

```c
// 同步内存映射到磁盘

#ifdef _WIN32
#define MDB_MSYNC(addr, len, flags) \
    (!FlushViewOfFile(addr, len))
#else
#define MDB_MSYNC(addr, len, flags) \
    msync(addr, len, flags)
#endif

// MS_ASYNC - 异步同步
// MS_SYNC - 同步同步
// MS_INVALIDATE - 使其他映射的缓存失效
```

---

## 13.5 性能优化技巧

### 批量写入

```c
// 使用 writev 批量写入页面

struct iovec iov[MDB_COMMIT_PAGES];
size_t niov = 0;

// 收集连续页面
for (i = start; i < end && niov < MDB_COMMIT_PAGES; i++) {
    iov[niov].iov_base = page[i];
    iov[niov].iov_len = env->me_psize;
    niov++;
}

// 一次性写入
wsize = pwritev(env->me_fd, iov, niov, offset);
```

### 关闭预读

```c
// 对于大数据库，预读会浪费内存

#ifdef MADV_RANDOM
madvise(env->me_map, env->me_mapsize, MADV_RANDOM);
#endif

#ifdef POSIX_MADV_RANDOM
posix_madvise(env->me_map, env->me_mapsize,
              POSIX_MADV_RANDOM);
#endif
```

### 内存对齐优化

```c
// 利用对齐访问提高性能

// 检查是否支持未对齐访问
#ifdef MISALIGNED_OK
// 可以直接访问未对齐数据
#define COPY_PGNO(dst, src) dst = src
#else
// 需要逐字节复制
#define COPY_PGNO(dst, src) do { \
    unsigned short *s, *d; \
    s = (unsigned short *)&(src); \
    d = (unsigned short *)&(dst); \
    *d++ = *s++; \
    *d = *s; \
} while (0)
#endif
```

---

## 13.6 调试与诊断

### 平台特定的调试

```bash
# Linux - 使用 strace
strace -e trace=mmap,munmap,pwrite,pwritev,fsync ./your_program

# macOS - 使用 dtrace
dtrace -n 'syscall::mmap:entry { printf("%s", probefunc); }'

# FreeBSD - 使用 truss
truss -f -o trace.log ./your_program

# Windows - 使用 Process Monitor
# 监控文件 I/O 和注册表访问
```

### 性能分析

```bash
# Linux - perf
perf record ./your_program
perf report

# macOS - Instruments
# 使用 Time Profiler

# FreeBSD - pmcstat
pmcstat -O ./your_program
```

---

## 13.7 跨平台最佳实践

### 可移植的代码

```c
// 1. 使用标准类型
#include <stdint.h>
uint32_t value;  // 而不是 unsigned long
pgno_t pgno;      // 平台特定的页号类型

// 2. 使用抽象宏
#ifdef _WIN32
#define close(fd) CloseHandle(fd)
#else
#include <unistd.h>
#endif

// 3. 检查字节序
#if BYTE_ORDER == LITTLE_ENDIAN
// 小端序处理
#else
// 大端序处理
#endif

// 4. 使用平台抽象层
// LOCK_MUTEX, UNLOCK_MUTEX
// COPY_PGNO
// etc.
```

### 条件编译

```c
// 组织平台特定代码

#if defined(_WIN32)
    // Windows 特定代码
#elif defined(__APPLE__)
    // macOS 特定代码
#elif defined(__linux__)
    // Linux 特定代码
#elif defined(__FreeBSD__)
    // FreeBSD 特定代码
#elif defined(__NetBSD__)
    // NetBSD 特定代码
#else
    #error "Unsupported platform"
#endif

// 使用特性测试宏
#ifdef MDB_USE_POSIX_MUTEX
    // POSIX 互斥量代码
#elif defined(MDB_USE_SYSV_SEM)
    // System V 信号量代码
#elif defined(_WIN32)
    // Windows 互斥量代码
#endif
```

---

## 13.8 今日练习

### 练习 1: 比较平台性能

```c
// 在不同平台上测试性能
#include <time.h>

void benchmark_put(MDB_env *env, int count) {
    struct timespec start, end;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;

    clock_gettime(CLOCK_MONOTONIC, &start);

    mdb_txn_begin(env, NULL, 0, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);

    for (int i = 0; i < count; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key_%d", i);
        key = (MDB_val){buf, strlen(buf)};
        data = (MDB_val){"data", 4};
        mdb_put(txn, dbi, &key, &data, 0);
    }

    mdb_txn_commit(txn);

    clock_gettime(CLOCK_MONOTONIC, &end);

    long elapsed = (end.tv_sec - start.tv_sec) * 1000000000 +
                   (end.tv_nsec - start.tv_nsec);
    printf("Time: %ld ns (%.2f ns/op)\n",
           elapsed, (double)elapsed / count);
}
```

---

## 13.9 常见问题解答

### Q1: 为什么 Windows 使用 NT 原生 API 而不是 Win32 API？

**A:** NT API 的优势：

```
Win32 API 限制：
1. 文件映射大小限制 (32位)
2. 不支持异步 I/O 通知
3. 错误处理不够精细

NT Native API 优势：
1. NtCreateSection 支持 64位映射
2. NtMapViewOfSection 灵活性更高
3. 直接访问内核功能
4. 更好的性能控制

LMDB 使用：
- NtCreateSection: 创建内存映射
- NtMapViewOfSection: 映射视图
- NtFlushBuffersFile: 刷新缓冲区
```

### Q2: Linux 的 fdatasync 为什么有问题？

**A:** fdatasync 的历史问题：

```
问题背景：
1. 早期 Linux (2.6.x) 的 fdatasync 实现有 bug
   - 可能不同步元数据
   - 导致崩溃后数据损坏

2. 延迟分配 (delayed allocation)
   - 数据实际写入时间不确定
   - fdatasync 后数据可能仍在缓存

LMDB 解决方案：
1. 使用 fsync 作为默认选项
2. 提供 MDB_NOMETASYNC 标志
3. 开发者可以选择性能或安全
```

### Q3: MIPS 为什么需要显式刷新缓存？

**A:** MIPS 架构特性：

```
MIPS 缓存一致性：
1. 数据缓存和指令缓存分离
2. 写操作可能只更新数据缓存
3. 指令缓存可能看到旧数据

LMDB 处理：
#if defined(__mips__)
    // 写入后刷新缓存
    __builtin___clear_cache(ptr, ptr + size);
#endif

影响：
- mmap 的可执行代码需要刷新
- JIT 编译器需要特别处理
- 自修改代码需要缓存同步
```

### Q4: 如何编写可移植的跨平台代码？

**A:** 跨平台编程最佳实践：

```c
// 1. 使用平台抽象层
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <fcntl.h>
#endif

// 2. 统一的错误处理
int platform_sync(int fd) {
#ifdef _WIN32
    return FlushFileBuffers((HANDLE)_get_osfhandle(fd)) ? 0 : -1;
#else
    return fsync(fd);
#endif
}

// 3. 类型定义
typedef uintptr_t pgno_t;  // 页号类型
typedef uint64_t   txnid_t; // 事务ID类型

// 4. 内存屏障
#define MEMORY_BARRIER() \
    __asm__ __volatile__("" ::: "memory")

// 5. 字节序处理
#ifdef __BYTE_ORDER__
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        // 小端序代码
    #else
        // 大端序代码
    #endif
#endif
```

---

## 13.10 思考题

1. 为什么 Windows 使用 NT 原生 API 而不是 Win32 API？
2. Linux 的 fdatasync 为什么有问题？
3. MIPS 为什么需要显式刷新缓存？
4. 如何编写可移植的跨平台代码？
5. 不同平台上的性能差异有多大？

---

## 明天预告

Day 14 是我们的最后一课！我们将学习：
- 高级主题和最佳实践
- 常见陷阱和注意事项
- 性能调优技巧
- 实际应用案例

完成最后一天的学习，你将成为 LMDB 专家！

---
**参考文献：**
- mdb.c: 1-500 (平台兼容性宏)
- mdb.c: 4540-4650 (mdb_env_map)
- mdb.c: Windows 特定代码段
