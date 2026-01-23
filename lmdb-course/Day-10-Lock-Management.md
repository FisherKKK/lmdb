# LMDB 底层实现 14天课程 - Day 10

## 锁管理 - 并发控制的基础

欢迎回来！今天我们将深入 LMDB 的锁管理机制。虽然 LMDB 使用 MVCC 实现读写不阻塞，但仍需要锁来协调某些关键操作。

---

## 今天的目标

1. 理解 LMDB 的锁模型
2. 掌握读者锁的实现
3. 掌握写入者锁的实现
4. 了解不同平台的锁机制

---

## 10.1 LMDB 的锁模型

### 锁的类型

```
LMDB 使用两种锁：

1. 读者锁 (Reader Mutex / Reader Semaphore)
   • 保护读者表 (MDB_reader array)
   • 读事务注册/注销时使用
   • 短暂持有

2. 写入者锁 (Writer Mutex / Writer Semaphore)
   • 保护写事务
   • 同时只能有一个写事务
   • 写事务期间持有
```

### 锁的使用场景

```
场景                    | 需要的锁
------------------------|------------------------
读事务开始              | 读者锁（短暂）
读事务结束              | 读者锁（短暂）
写事务开始              | 写入者锁
写事务提交              | 写入者锁
写事务中止              | 写入者锁
页面分配                | 写入者锁
空闲列表操作            | 写入者锁
元数据更新              | 写入者锁
```

---

## 10.2 锁的抽象层

### 平台特定的锁类型

```c
// Unix - POSIX 互斥量
#ifdef MDB_USE_POSIX_MUTEX
typedef pthread_mutex_t mdb_mutex_t;
#define LOCK_MUTEX0(mutex)   pthread_mutex_lock(&mutex)
#define UNLOCK_MUTEX(mutex)  pthread_mutex_unlock(&mutex)

// Unix - POSIX 信号量
#elif defined(MDB_USE_POSIX_SEM)
typedef sem_t *mdb_mutex_t;
#define LOCK_MUTEX0(mutex)   mdb_sem_wait(mutex)
#define UNLOCK_MUTEX(mutex)  sem_post(mutex)

// Unix - System V 信号量
#elif defined(MDB_USE_SYSV_SEM)
typedef struct mdb_mutex {
    int semid;
    int semnum;
    int *locked;
} mdb_mutex_t[1];

// Windows - Windows 互斥量
#elif defined(_WIN32)
typedef HANDLE mdb_mutex_t;
#define LOCK_MUTEX0(mutex)   WaitForSingleObject(mutex, INFINITE)
#define UNLOCK_MUTEX(mutex)  ReleaseMutex(mutex)
#endif
```

---

## 10.3 读者锁

### 获取读者锁

```c
// 读事务注册时获取读者锁
if (LOCK_MUTEX(rc, env, env->me_rmutex))
    return rc;

// 查找/分配读者槽位
for (i = 0; i < env->me_maxreaders; i++) {
    if (ti->mti_readers[i].mr_pid == 0)
        break;
}

// 注册读者
r = &ti->mti_readers[i];
r->mr_pid = env->me_pid;
r->mr_tid = pthread_self();

// 释放读者锁
UNLOCK_MUTEX(env->me_rmutex);
```

### 读者锁的作用

```
读者锁保护的操作：

1. mti_numreaders
   • 当前读者数量

2. mti_readers[i].mr_pid
   • 读者进程ID

3. mti_readers[i].mr_txnid
   • 读者事务ID

注意：读者锁只在注册/注销时使用，
      实际读取数据时不持有锁！
```

---

## 10.4 写入者锁

### 获取写入者锁

```c
// 写事务开始时获取写入者锁
if (LOCK_MUTEX(rc, env, env->me_wmutex))
    return rc;

// 检查是否已有活跃写事务
if (env->me_txn) {
    UNLOCK_MUTEX(env->me_wmutex);
    return MDB_TXN_FULL;
}

// 设置为活跃写事务
env->me_txn = txn;

// 分配新事务ID
txn->mt_txnid = ti->mti_txnid + 1;

// 注意：写入者锁在整个写事务期间持有
```

### 释放写入者锁

```c
// 写事务提交/中止时释放写入者锁

// 清除活跃写事务
env->me_txn = NULL;

// 更新全局事务ID
ti->mti_txnid = txn->mt_txnid;

// 释放写入者锁
UNLOCK_MUTEX(env->me_wmutex);
```

---

## 10.5 不同平台的实现

### POSIX 互斥量 (Linux, macOS)

```c
// 创建互斥量
pthread_mutexattr_t attr;
pthread_mutexattr_init(&attr);
pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
pthread_mutex_init(&mutex, &attr);

// 锁定
int rc = pthread_mutex_lock(&mutex);
if (rc == EOWNERDEAD) {
    // 互斥量的前拥有者崩溃
    // 恢复互斥量
    pthread_mutex_consistent(&mutex);
}

// 解锁
pthread_mutex_unlock(&mutex);
```

### POSIX 信号量 (BSD, Haiku)

```c
// 打开命名信号量
sem_t *sem = sem_open("/lmdb_mydb", O_CREAT, 0664, 1);

// 等待（锁定）
int mdb_sem_wait(sem_t *sem) {
    int rc;
    while ((rc = sem_wait(sem)) && (rc = errno) == EINTR)
        ;  // 重试中断
    return rc;
}

// 释放（解锁）
sem_post(sem);
```

### System V 信号量 (NetBSD, 某些 BSD)

```c
// 创建信号量集
int semid = semget(IPC_PRIVATE, 1, IPC_CREAT | 0664);

// 初始化
union semun {
    int val;
    struct semid_ds *buf;
} arg;
arg.val = 1;
semctl(semid, 0, SETVAL, arg);

// 等待
struct sembuf sb = {0, -1, SEM_UNDO};
semop(semid, &sb, 1);

// 释放
struct sembuf sb = {0, 1, SEM_UNDO};
semop(semid, &sb, 1);
```

### Windows 互斥量

```c
// 创建互斥量
HANDLE mutex = CreateMutex(NULL, FALSE, "lmdb_mydb");

// 等待
WaitForSingleObject(mutex, INFINITE);

// 释放
ReleaseMutex(mutex);
```

---

## 10.6 健壮锁 (Robust Mutex)

### 什么是健壮锁？

```
健壮锁特性：
  • 检测拥有者崩溃
  • 自动恢复到可用状态
  • 避免死锁
```

### 使用场景

```c
// 当进程在持有锁时崩溃：
// 1. 健壮锁检测到崩溃
// 2. 下一个等待者收到 EOWNERDEAD
// 3. 等待者恢复锁状态
// 4. 继续执行

// 示例代码
#ifdef MDB_USE_ROBUST
int rc = pthread_mutex_lock(&mutex);
if (rc == EOWNERDEAD) {
    // 前拥有者崩溃
    rc = pthread_mutex_consistent(&mutex);
    if (rc) {
        // 无法恢复
        return MDB_PANIC;
    }
    // 锁已恢复，可以继续
}
#endif
```

---

## 10.7 锁文件布局

### 锁文件结构

```
┌─────────────────────────────────────────────────────────────┐
│  锁文件 (lock.mdb)                                          │
├─────────────────────────────────────────────────────────────┤
│  Offset 0      │ mti_rmutex (读者锁)                        │
├─────────────────────────────────────────────────────────────┤
│  Offset X      │ mti_wmutex (写入者锁)                      │
├─────────────────────────────────────────────────────────────┤
│  Offset Y      │ mti_numreaders (读者数量)                  │
├─────────────────────────────────────────────────────────────┤
│  Offset Z      │ padding                                    │
├─────────────────────────────────────────────────────────────┤
│  Offset Z+8    │ mti_readers[0] (读者槽位 0)                │
│                │   - mr_txnid                               │
│                │   - mr_pid                                 │
│                │   - mr_tid                                 │
│                │   - mr_flags                               │
├─────────────────────────────────────────────────────────────┤
│  Offset Z+8+24 │ mti_readers[1] (读者槽位 1)                │
├─────────────────────────────────────────────────────────────┤
│  ...                                                         │
├─────────────────────────────────────────────────────────────┤
│  Offset Z+8+24*N │ mti_readers[N] (读者槽位 N)              │
└─────────────────────────────────────────────────────────────┘
```

### 锁文件大小计算

```c
size_t rsize = sizeof(MDB_txninfo) +
               env->me_maxreaders * sizeof(MDB_reader);

// 示例：
// sizeof(MDB_txninfo) ≈ 256 字节
// sizeof(MDB_reader) = 24 字节
// me_maxreaders = 126
// rsize = 256 + 126 * 24 = 256 + 3024 = 3280 字节
```

---

## 10.8 MDB_NOLOCK 模式

### 无锁模式

```c
// 使用 MDB_NOLOCK 标志打开环境
mdb_env_open(env, path, MDB_NOLOCK, 0664);

// 特点：
// 1. 不创建/使用锁文件
// 2. 只允许单个进程访问
// 3. 适合只读访问或单进程应用
// 4. 在只读文件系统上可用
```

### 无锁模式的事务

```c
// 读事务
if (!ti) {  // 无锁模式
    meta = mdb_env_pick_meta(env);
    txn->mt_txnid = meta->mm_txnid;
    txn->mt_u.reader = NULL;  // 无读者槽位
} else {
    // 正常模式：需要注册读者槽位
    // ...
}

// 写事务
if (!ti) {  // 无锁模式
    meta = mdb_env_pick_meta(env);
    txn->mt_txnid = meta->mm_txnid;
    // 不需要获取写入者锁
} else {
    // 正常模式：需要获取写入者锁
    LOCK_MUTEX(env->me_wmutex);
    // ...
}
```

---

## 10.9 调试锁问题

### 检测锁竞争

```bash
# Linux - 使用 strace 观察锁调用
strace -e trace=futex,semop ./your_program

# macOS - 使用 dtrace
dtrace -n 'pthread_mutex_lock { printf("%s", probefunc); }'

# 观察锁文件
ls -l lock.mdb
cat lock.mdb | hexdump -C | head
```

### 常见问题

```
问题 1: MDB_READERS_FULL
原因：  读者表满了
解决：  增加 me_maxreaders

问题 2: MDB_TXN_FULL
原因：  同时有多个写事务
解决：  等待现有写事务完成

问题 3: 锁文件损坏
原因：  进程崩溃或系统故障
解决：  删除 lock.mdb 并重启

问题 4: 信号量被其他用户拥有 (BSD)
原因：  权限问题
解决：  以正确用户运行或清除信号量
```

---

## 10.10 今日练习

### 练习 1: 观察锁行为

```bash
# 终端 1: 运行一个长时间读事务
cat > long_reader.c << 'EOF'
MDB_txn *txn;
mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
sleep(30);  // 持续30秒
mdb_txn_abort(txn);
EOF

# 终端 2: 观察读者表
watch -n 1 'hexdump -C lock.mdb | grep "mr_pid"'

# 终端 3: 尝试开始写事务
# 应该能成功（读事务不阻塞写事务）
```

### 练习 2: 测试读者限制

```c
// 创建大量读事务，测试读者表限制
#define MAX_READERS 10

pthread_t threads[MAX_READERS];

void *reader_thread(void *arg) {
    MDB_env *env = arg;
    MDB_txn *txn;

    int rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc) {
        printf("Failed: %s\n", mdb_strerror(rc));
        return NULL;
    }

    sleep(10);
    mdb_txn_abort(txn);
    return NULL;
}

// 创建 MAX_READERS 个线程
for (int i = 0; i < MAX_READERS; i++) {
    pthread_create(&threads[i], NULL, reader_thread, env);
}

// 尝试创建第 MAX_READERS + 1 个线程
// 应该返回 MDB_READERS_FULL
```

---

## 10.11 常见问题解答

### Q1: 为什么读事务不需要在读取数据时持有锁？

**A:** LMDB 的 MVCC 架构：

```
传统数据库：
  读操作 -> 加读锁 -> 访问数据 -> 释放锁
  写操作等待读锁释放

LMDB:
  读事务 -> 记录快照ID -> 读取快照版本
  写操作 -> 创建新版本 -> 不阻塞读操作

关键点：
1. 读事务只读不写，不会修改数据结构
2. 写操作创建新页面，原页面保持不变
3. 元数据页交替更新，保证一致性
```

### Q2: 读者锁和写入者锁可以同时持有吗？

**A:** 可以，而且在设计上就是并发的：

```
并发场景：
  读事务1 (txnid=10) -> 读者表槽位0
  读事务2 (txnid=10) -> 读者表槽位1
  读事务3 (txnid=10) -> 读者表槽位2
  写事务   (txnid=11) -> 写入者锁

读事务之间：无锁竞争
读事务 vs 写事务：无阻塞（MVCC）
写事务 vs 写事务：互斥（只有一个能活跃）
```

### Q3: MDB_NOLOCK 模式有什么风险？

**A:** NOLOCK 模式的风险：

```
风险 1: 失去崩溃恢复保证
  - 无法检测其他进程的写事务
  - 可能读取到不一致的数据

风险 2: 多进程写入冲突
  - 没有写入者锁协调
  - 可能损坏数据库

风险 3: 读者表管理混乱
  - 无法追踪活跃读事务
  - 可能回收仍在使用的页面

适用场景：
  - 单进程只读访问
  - 数据库位于只读文件系统
  - 备份和恢复操作
```

### Q4: 健壮锁如何避免死锁？

**A:** 健壮锁（Robust Lock）特性：

```c
// 健壮锁在 POSIX 中的定义
pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);

// 工作原理：
// 1. 持有锁的进程崩溃时
// 2. 下一个获取锁的线程会返回 EOWNERDEAD
// 3. 线程检测到状态，清理并继续

LMDB 使用：
if (lock_ret == EOWNERDEAD) {
    // 清理可能的不一致状态
    // 标记需要恢复
    // 继续操作
}
```

---

## 10.12 思考题

1. 为什么读事务不需要在读取数据时持有锁？
2. 读者锁和写入者锁可以同时持有吗？
3. MDB_NOLOCK 模式有什么风险？
4. 健壮锁如何避免死锁？
5. 如何调试锁相关的性能问题？

---

## 明天预告

Day 11 将深入讲解写操作与写时复制。我们将学习：
- 页面的修改机制
- 写时复制的实现
- 页面分裂和合并
- 溢出页的处理

写时复制是 LMDB 保证原子性的关键！

---
**参考文献：**
- mdb.c: 400-500 (锁宏定义)
- mdb.c: 3090-3120 (读者锁使用)
- mdb.c: 3140-3160 (写入者锁使用)
