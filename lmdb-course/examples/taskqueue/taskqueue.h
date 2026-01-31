/**
 * taskqueue.h - Task Queue System Header
 *
 * A simple but complete task queue system built with LMDB
 */

#ifndef TASKQUEUE_H
#define TASKQUEUE_H

#include <lmdb.h>
#include <stdint.h>
#include <time.h>

// 任务状态常量
#define STATUS_PENDING   "pending"
#define STATUS_RUNNING   "running"
#define STATUS_COMPLETED "completed"
#define STATUS_FAILED    "failed"

// 数据库命名常量
#define DB_TASKS      "tasks"       // 任务数据
#define DB_PENDING    "pending"     // 待处理队列
#define DB_RUNNING    "running"     // 运行中队列
#define DB_COMPLETED  "completed"   // 已完成任务
#define DB_FAILED     "failed"      // 失败任务
#define DB_STATS      "stats"       // 统计数据

// 最大常量
#define MAX_TASK_NAME 256
#define MAX_TASK_DATA 1024
#define MAX_RESULT     512
#define MAX_WORKER_ID  64

/**
 * 任务数据结构
 */
typedef struct {
    uint64_t task_id;          // 任务 ID
    char name[MAX_TASK_NAME];  // 任务名称
    char data[MAX_TASK_DATA];  // 任务数据（可以是 JSON）
    int priority;              // 优先级（0-10，10最高）
    uint64_t depends_on;       // 依赖的任务 ID（0表示无依赖）
    char status[16];           // pending/running/completed/failed
    time_t created_at;         // 创建时间
    time_t started_at;         // 开始时间
    time_t completed_at;       // 完成时间
    char result[MAX_RESULT];   // 结果信息
    char worker_id[MAX_WORKER_ID]; // 执行者 ID
    int retry_count;           // 重试次数
} task_t;

/**
 * 队列统计信息
 */
typedef struct {
    uint64_t total_tasks;
    uint64_t pending_tasks;
    uint64_t running_tasks;
    uint64_t completed_tasks;
    uint64_t failed_tasks;
} queue_stats_t;

/**
 * 任务队列环境
 */
typedef struct {
    MDB_env *env;
    int max_map_size;
    int max_dbs;
    int max_readers;
    int running;
} taskqueue_t;

// API 函数声明

// 初始化和清理
int taskqueue_init(taskqueue_t *tq, const char *path, size_t map_size);
void taskqueue_close(taskqueue_t *tq);

// 任务操作
int task_create(taskqueue_t *tq, task_t *task);
int task_get(taskqueue_t *tq, uint64_t task_id, task_t *task);
int task_update_status(taskqueue_t *tq, uint64_t task_id, const char *status, const char *result);
int task_delete(taskqueue_t *tq, uint64_t task_id);

// 队列操作（用于工作线程）
int task_acquire(taskqueue_t *tq, const char *worker_id, task_t *task);
int task_complete(taskqueue_t *tq, uint64_t task_id, const char *result);
int task_fail(taskqueue_t *tq, uint64_t task_id, const char *error);

// 统计和查询
int taskqueue_get_stats(taskqueue_t *tq, queue_stats_t *stats);
int taskqueue_list(taskqueue_t *tq, const char *status, void (*callback)(task_t*, void*), void *arg);

// 工具函数
uint64_t task_generate_id(taskqueue_t *tq);
int task_validate(task_t *task);

#endif // TASKQUEUE_H
