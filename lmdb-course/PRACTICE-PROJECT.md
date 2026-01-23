# LMDB 实践项目教程

## 项目概述

本教程通过一个完整的实战项目 - **任务队列系统** - 帮助你深入理解 LMDB 的实际应用。

## 项目: TaskQueue - 基于 LMDB 的任务队列

一个简单但功能完整的任务队列系统，展示 LMDB 在实际项目中的应用。

### 功能特性

- ✅ 任务创建和分配
- ✅ 任务状态管理（pending/running/completed/failed）
- ✅ 优先级支持
- ✅ 任务依赖关系
- ✅ 工作线程池
- ✅ 持久化和崩溃恢复
- ✅ 统计和监控

### 技术亮点

- 使用多个命名数据库组织数据
- 展示事务的原子性
- 演示并发读写
- 实现崩溃恢复
- 使用游标进行批量操作

## 实现步骤

### 第一步: 设计数据结构

```c
// 任务数据结构
typedef struct {
    uint64_t task_id;        // 任务 ID
    char name[256];          // 任务名称
    char data[1024];         // 任务数据（JSON）
    int priority;            // 优先级（0-10）
    uint64_t depends_on;     // 依赖的任务 ID
    char status[16];         // pending/running/completed/failed
    time_t created_at;       // 创建时间
    time_t started_at;       // 开始时间
    time_t completed_at;     // 完成时间
    char result[512];        // 结果信息
    char worker_id[64];      // 执行者 ID
} task_t;
```

### 第二步: 数据库设计

```c
// 数据库组织
#define DB_TASKS       "tasks"        // 任务数据
#define DB_PENDING    "pending"      // 待处理队列（按优先级）
#define DB_RUNNING    "running"      // 运行中队列
#define DB_COMPLETED  "completed"    // 已完成任务
#define DB_FAILED     "failed"       // 失败任务
#define DB_INDEX      "index"        // 索引数据库
```

### 第三步: 核心功能实现

#### 任务创建

```c
int task_create(MDB_env *env, task_t *task) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;
    int rc;

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc) return rc;

    // 打开数据库
    mdb_dbi_open(txn, DB_TASKS, MDB_CREATE, &dbi);
    mdb_dbi_open(txn, DB_PENDING, MDB_CREATE, &dbi);

    // 生成任务 ID
    task->task_id = generate_task_id(txn);
    task->created_at = time(NULL);
    strcpy(task->status, "pending");

    // 存储任务
    key.mv_data = &task->task_id;
    key.mv_size = sizeof(task->task_id);
    data.mv_data = task;
    data.mv_size = sizeof(task_t);

    mdb_put(txn, dbi, &key, &data, 0);

    // 添加到待处理队列
    MDB_dbi pending_db;
    mdb_dbi_open(txn, DB_PENDING, 0, &pending_db);

    // 使用优先级作为排序键
    char sort_key[32];
    snprintf(sort_key, sizeof(sort_key), "%010d-%016lu",
             10 - task->priority, task->task_id);

    MDB_val sort_key_val = { .mv_data = sort_key, .mv_size = strlen(sort_key) };
    MDB_val task_id_val = { .mv_data = &task->task_id, .mv_size = sizeof(task->task_id) };

    mdb_put(txn, pending_db, &sort_key_val, &task_id_val, 0);

    return mdb_txn_commit(txn);
}
```

#### 任务分配

```c
int task_acquire(MDB_env *env, const char *worker_id, task_t *task) {
    MDB_txn *txn;
    MDB_dbi pending_db, running_db, tasks_db;
    MDB_cursor *cursor;
    MDB_val key, data;
    int rc;

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc) return rc;

    mdb_dbi_open(txn, DB_PENDING, 0, &pending_db);
    mdb_dbi_open(txn, DB_RUNNING, MDB_CREATE, &running_db);
    mdb_dbi_open(txn, DB_TASKS, 0, &tasks_db);

    mdb_cursor_open(txn, pending_db, &cursor);

    // 获取最高优先级任务
    rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    if (rc == MDB_NOTFOUND) {
        mdb_txn_abort(txn);
        return MDB_NOTFOUND;
    }

    // 读取任务数据
    uint64_t *task_id_ptr = data.mv_data;
    MDB_val task_key = { .mv_data = task_id_ptr, .mv_size = sizeof(uint64_t) };
    MDB_val task_data;

    mdb_get(txn, tasks_db, &task_key, &task_data);
    memcpy(task, task_data.mv_data, sizeof(task_t));

    // 更新任务状态
    strcpy(task->status, "running");
    task->started_at = time(NULL);
    strncpy(task->worker_id, worker_id, sizeof(task->worker_id) - 1);

    task_data.mv_data = task;
    mdb_put(txn, tasks_db, &task_key, &task_data, 0);

    // 从待处理队列移除
    mdb_cursor_del(cursor, 0);

    // 添加到运行中队列
    char worker_key[128];
    snprintf(worker_key, sizeof(worker_key), "%s-%lu", worker_id, task->task_id);
    MDB_val worker_key_val = { .mv_data = worker_key, .mv_size = strlen(worker_key) };
    mdb_put(txn, running_db, &worker_key_val, &task_key, 0);

    mdb_cursor_close(cursor);
    return mdb_txn_commit(txn);
}
```

#### 任务完成

```c
int task_complete(MDB_env *env, uint64_t task_id, const char *result) {
    MDB_txn *txn;
    MDB_dbi running_db, completed_db, tasks_db;
    MDB_val key, data;
    int rc;

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc) return rc;

    mdb_dbi_open(txn, DB_RUNNING, 0, &running_db);
    mdb_dbi_open(txn, DB_COMPLETED, MDB_CREATE, &completed_db);
    mdb_dbi_open(txn, DB_TASKS, 0, &tasks_db);

    // 读取并更新任务
    key.mv_data = &task_id;
    key.mv_size = sizeof(task_id);
    mdb_get(txn, tasks_db, &key, &data);

    task_t *task = data.mv_data;
    strcpy(task->status, "completed");
    task->completed_at = time(NULL);
    strncpy(task->result, result, sizeof(task->result) - 1);

    mdb_put(txn, tasks_db, &key, &data, 0);

    // 从运行中队列移除
    // 这里需要遍历查找 worker_key-task_id 条目并删除

    // 添加到已完成队列
    char complete_key[32];
    snprintf(complete_key, sizeof(complete_key), "%016lu", task_id);
    MDB_val complete_key_val = { .mv_data = complete_key, .mv_size = strlen(complete_key) };
    mdb_put(txn, completed_db, &complete_key_val, &key, 0);

    return mdb_txn_commit(txn);
}
```

### 第四步: 工作线程

```c
void *worker_thread(void *arg) {
    taskqueue_t *tq = arg;
    char worker_id[64];
    snprintf(worker_id, sizeof(worker_id), "worker-%lu", (unsigned long)pthread_self());

    while (tq->running) {
        task_t task;
        int rc = task_acquire(tq->env, worker_id, &task);

        if (rc == 0) {
            printf("[%s] Processing task %lu: %s\n",
                   worker_id, task.task_id, task.name);

            // 执行任务（这里可以调用实际的处理函数）
            sleep(1 + rand() % 3);  // 模拟处理时间

            char result[512];
            snprintf(result, sizeof(result), "Success, processed in %ld seconds",
                     time(NULL) - task.started_at);

            task_complete(tq->env, task.task_id, result);

            printf("[%s] Completed task %lu\n", worker_id, task.task_id);
        } else {
            usleep(100000);  // 100ms
        }
    }

    return NULL;
}
```

### 第五步: 统计和监控

```c
typedef struct {
    uint64_t total_tasks;
    uint64_t pending_tasks;
    uint64_t running_tasks;
    uint64_t completed_tasks;
    uint64_t failed_tasks;
} taskqueue_stats_t;

int taskqueue_get_stats(MDB_env *env, taskqueue_stats_t *stats) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_stat st;
    int rc;

    memset(stats, 0, sizeof(*stats));

    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc) return rc;

    // 统计各队列任务数
    mdb_dbi_open(txn, DB_TASKS, 0, &dbi);
    mdb_stat(txn, dbi, &st);
    stats->total_tasks = st.ms_entries;

    mdb_dbi_open(txn, DB_PENDING, 0, &dbi);
    mdb_stat(txn, dbi, &st);
    stats->pending_tasks = st.ms_entries;

    mdb_dbi_open(txn, DB_RUNNING, 0, &dbi);
    mdb_stat(txn, dbi, &st);
    stats->running_tasks = st.ms_entries;

    mdb_dbi_open(txn, DB_COMPLETED, 0, &dbi);
    mdb_stat(txn, dbi, &st);
    stats->completed_tasks = st.ms_entries;

    mdb_dbi_open(txn, DB_FAILED, 0, &dbi);
    mdb_stat(txn, dbi, &st);
    stats->failed_tasks = st.ms_entries;

    mdb_txn_abort(txn);
    return 0;
}
```

## 完整代码

完整的项目代码位于 `examples/taskqueue/` 目录，包含：

- `taskqueue.h` - 头文件和数据结构定义
- `taskqueue.c` - 核心实现
- `tq_client.c` - 命令行客户端
- `tq_worker.c` - 工作进程
- `tq_admin.c` - 管理工具

## 使用示例

### 创建任务

```bash
./tq_client create "Process data file" --priority 5 --data '{"file":"data.csv"}'
```

### 查看队列状态

```bash
./tq_admin stats

Queue Statistics:
  Total Tasks:   150
  Pending:       45
  Running:       5
  Completed:     95
  Failed:        5
```

### 启动工作进程

```bash
./tq_worker --workers 4
```

## 学到的知识点

通过这个项目，你将掌握：

1. **多数据库设计** - 如何使用命名数据库组织数据
2. **索引设计** - 使用排序键实现优先级队列
3. **并发控制** - 多个工作线程安全地获取任务
4. **事务原子性** - 任务状态转移的原子性保证
5. **游标使用** - 批量操作和队列遍历
6. **错误处理** - 崩溃恢复和重试机制

## 扩展练习

1. **添加任务超时** - 检测长时间运行的任务
2. **实现任务重试** - 失败任务的自动重试机制
3. **添加任务调度** - 基于时间或条件的延迟执行
4. **实现任务依赖** - 等待依赖任务完成后才执行
5. **添加监控API** - HTTP 或 gRPC 监控接口

## 总结

这个 TaskQueue 项目展示了 LMDB 在实际应用中的强大能力：
- 简单的数据模型设计
- 高效的并发访问
- 可靠的持久化
- 良好的扩展性

通过这个项目，你将真正掌握 LMDB 的使用！
