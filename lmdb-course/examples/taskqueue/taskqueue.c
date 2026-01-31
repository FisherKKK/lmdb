/**
 * taskqueue.c - Task Queue System Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "taskqueue.h"

// 全局互斥锁（用于环境操作）
static pthread_mutex_t tq_mutex = PTHREAD_MUTEX_INITIALIZER;

// 初始化任务队列环境
int taskqueue_init(taskqueue_t *tq, const char *path, size_t map_size) {
    int rc;

    if (!tq || !path) return -1;

    memset(tq, 0, sizeof(taskqueue_t));

    // 创建 LMDB 环境
    rc = mdb_env_create(&tq->env);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_create failed: %s\n", mdb_strerror(rc));
        return rc;
    }

    // 设置映射大小
    rc = mdb_env_set_mapsize(tq->env, map_size);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_set_mapsize failed: %s\n", mdb_strerror(rc));
        mdb_env_close(tq->env);
        return rc;
    }

    // 设置最大数据库数
    rc = mdb_env_set_maxdbs(tq->env, 10);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_set_maxdbs failed: %s\n", mdb_strerror(rc));
        mdb_env_close(tq->env);
        return rc;
    }

    // 设置最大读者数
    rc = mdb_env_set_maxreaders(tq->env, 64);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_set_maxreaders failed: %s\n", mdb_strerror(rc));
        mdb_env_close(tq->env);
        return rc;
    }

    // 打开环境
    rc = mdb_env_open(tq->env, path, 0, 0664);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_open failed: %s\n", mdb_strerror(rc));
        mdb_env_close(tq->env);
        return rc;
    }

    tq->running = 1;
    printf("TaskQueue initialized at %s (map size: %zu MB)\n",
           path, map_size / (1024 * 1024));

    return 0;
}

// 关闭任务队列环境
void taskqueue_close(taskqueue_t *tq) {
    if (tq && tq->env) {
        tq->running = 0;
        mdb_env_close(tq->env);
        tq->env = NULL;
        printf("TaskQueue closed\n");
    }
}

// 生成新的任务 ID
uint64_t task_generate_id(taskqueue_t *tq) {
    static uint64_t id_counter = 1;
    static pthread_mutex_t id_mutex = PTHREAD_MUTEX_INITIALIZER;
    uint64_t id;

    pthread_mutex_lock(&id_mutex);
    id = id_counter++;
    pthread_mutex_unlock(&id_mutex);

    return id;
}

// 验证任务数据
int task_validate(task_t *task) {
    if (!task) return -1;
    if (task->priority < 0 || task->priority > 10) return -1;
    if (strlen(task->name) == 0) return -1;
    return 0;
}

// 创建任务
int task_create(taskqueue_t *tq, task_t *task) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;
    char sort_key[32];
    int rc;

    if (!tq || !task) return -1;

    if (task_validate(task) != 0) {
        fprintf(stderr, "Invalid task data\n");
        return -1;
    }

    pthread_mutex_lock(&tq_mutex);

    // 开始事务
    rc = mdb_txn_begin(tq->env, NULL, 0, &txn);
    if (rc != 0) {
        pthread_mutex_unlock(&tq_mutex);
        return rc;
    }

    // 生成任务 ID 和时间戳
    if (task->task_id == 0) {
        task->task_id = task_generate_id(tq);
    }
    task->created_at = time(NULL);
    strcpy(task->status, STATUS_PENDING);
    task->retry_count = 0;

    // 存储任务到主数据库
    rc = mdb_dbi_open(txn, DB_TASKS, MDB_CREATE, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        pthread_mutex_unlock(&tq_mutex);
        return rc;
    }

    key.mv_data = &task->task_id;
    key.mv_size = sizeof(task->task_id);
    data.mv_data = task;
    data.mv_size = sizeof(task_t);

    rc = mdb_put(txn, dbi, &key, &data, 0);
    if (rc != 0) {
        mdb_txn_abort(txn);
        pthread_mutex_unlock(&tq_mutex);
        return rc;
    }

    // 添加到待处理队列（按优先级排序）
    MDB_dbi pending_db;
    rc = mdb_dbi_open(txn, DB_PENDING, MDB_CREATE, &pending_db);
    if (rc != 0) {
        mdb_txn_abort(txn);
        pthread_mutex_unlock(&tq_mutex);
        return rc;
    }

    // 排序键：优先级（反转）+ 任务 ID
    snprintf(sort_key, sizeof(sort_key), "%02d-%020lu",
             10 - task->priority, task->task_id);

    MDB_val sort_key_val = { .mv_data = sort_key, .mv_size = strlen(sort_key) };
    rc = mdb_put(txn, pending_db, &sort_key_val, &key, 0);
    if (rc != 0) {
        mdb_txn_abort(txn);
        pthread_mutex_unlock(&tq_mutex);
        return rc;
    }

    pthread_mutex_unlock(&tq_mutex);

    return mdb_txn_commit(txn);
}

// 获取任务
int task_get(taskqueue_t *tq, uint64_t task_id, task_t *task) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;
    int rc;

    if (!tq || task_id == 0 || !task) return -1;

    rc = mdb_txn_begin(tq->env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) return rc;

    rc = mdb_dbi_open(txn, DB_TASKS, 0, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    key.mv_data = &task_id;
    key.mv_size = sizeof(task_id);

    rc = mdb_get(txn, dbi, &key, &data);
    if (rc == 0) {
        memcpy(task, data.mv_data, sizeof(task_t));
    }

    mdb_txn_abort(txn);
    return rc;
}

// 获取任务（用于工作线程）
int task_acquire(taskqueue_t *tq, const char *worker_id, task_t *task) {
    MDB_txn *txn;
    MDB_dbi pending_db, running_db, tasks_db;
    MDB_cursor *cursor;
    MDB_val key, data;
    int rc;

    if (!tq || !worker_id || !task) return -1;

    pthread_mutex_lock(&tq_mutex);

    rc = mdb_txn_begin(tq->env, NULL, 0, &txn);
    if (rc != 0) {
        pthread_mutex_unlock(&tq_mutex);
        return rc;
    }

    // 打开数据库
    mdb_dbi_open(txn, DB_PENDING, 0, &pending_db);
    mdb_dbi_open(txn, DB_RUNNING, MDB_CREATE, &running_db);
    mdb_dbi_open(txn, DB_TASKS, 0, &tasks_db);

    // 打开游标
    rc = mdb_cursor_open(txn, pending_db, &cursor);
    if (rc != 0) {
        mdb_txn_abort(txn);
        pthread_mutex_unlock(&tq_mutex);
        return rc;
    }

    // 获取最高优先级任务
    rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    if (rc != 0) {
        mdb_cursor_close(cursor);
        mdb_txn_abort(txn);
        pthread_mutex_unlock(&tq_mutex);
        return (rc == MDB_NOTFOUND) ? 0 : rc;
    }

    // 读取任务数据
    uint64_t *task_id_ptr = data.mv_data;
    MDB_val task_key = { .mv_data = task_id_ptr, .mv_size = sizeof(uint64_t) };
    MDB_val task_data;

    rc = mdb_get(txn, tasks_db, &task_key, &task_data);
    if (rc == 0) {
        memcpy(task, task_data.mv_data, sizeof(task_t));
    }

    // 更新任务状态
    strcpy(task->status, STATUS_RUNNING);
    task->started_at = time(NULL);
    strncpy(task->worker_id, worker_id, sizeof(task->worker_id) - 1);

    task_data.mv_data = task;
    task_data.mv_size = sizeof(task_t);

    mdb_put(txn, tasks_db, &task_key, &task_data, 0);

    // 从待处理队列移除
    mdb_cursor_del(cursor, 0);

    // 添加到运行中队列
    char running_key[128];
    snprintf(running_key, sizeof(running_key), "%s-%lu", worker_id, task->task_id);

    MDB_val running_key_val = { .mv_data = running_key, .mv_size = strlen(running_key) };
    mdb_put(txn, running_db, &running_key_val, &task_key, 0);

    mdb_cursor_close(cursor);
    pthread_mutex_unlock(&tq_mutex);

    return mdb_txn_commit(txn);
}

// 完成任务
int task_complete(taskqueue_t *tq, uint64_t task_id, const char *result) {
    MDB_txn *txn;
    MDB_dbi running_db, completed_db, tasks_db;
    MDB_val key, data;
    char complete_key[32];
    int rc;

    if (!tq || task_id == 0) return -1;

    pthread_mutex_lock(&tq_mutex);

    rc = mdb_txn_begin(tq->env, NULL, 0, &txn);
    if (rc != 0) {
        pthread_mutex_unlock(&tq_mutex);
        return rc;
    }

    mdb_dbi_open(txn, DB_RUNNING, 0, &running_db);
    mdb_dbi_open(txn, DB_COMPLETED, MDB_CREATE, &completed_db);
    mdb_dbi_open(txn, DB_TASKS, 0, &tasks_db);

    // 更新任务
    key.mv_data = &task_id;
    key.mv_size = sizeof(task_id);

    rc = mdb_get(txn, tasks_db, &key, &data);
    if (rc == 0) {
        task_t *task = data.mv_data;
        strcpy(task->status, STATUS_COMPLETED);
        task->completed_at = time(NULL);
        if (result) {
            strncpy(task->result, result, sizeof(task->result) - 1);
        }

        mdb_put(txn, tasks_db, &key, &data, 0);
    }

    // 从运行中队列移除（简化：需要遍历查找）
    // 在实际应用中，可能需要维护一个反向索引

    // 添加到已完成队列
    snprintf(complete_key, sizeof(complete_key), "%020lu", task_id);
    MDB_val complete_key_val = { .mv_data = complete_key, .mv_size = strlen(complete_key) };
    mdb_put(txn, completed_db, &complete_key_val, &key, 0);

    pthread_mutex_unlock(&tq_mutex);

    return mdb_txn_commit(txn);
}

// 标记任务失败
int task_fail(taskqueue_t *tq, uint64_t task_id, const char *error) {
    MDB_txn *txn;
    MDB_dbi running_db, failed_db, tasks_db;
    MDB_val key, data;
    char failed_key[32];
    int rc;

    if (!tq || task_id == 0) return -1;

    pthread_mutex_lock(&tq_mutex);

    rc = mdb_txn_begin(tq->env, NULL, 0, &txn);
    if (rc != 0) {
        pthread_mutex_unlock(&tq_mutex);
        return rc;
    }

    mdb_dbi_open(txn, DB_RUNNING, 0, &running_db);
    mdb_dbi_open(txn, DB_FAILED, MDB_CREATE, &failed_db);
    mdb_dbi_open(txn, DB_TASKS, 0, &tasks_db);

    // 更新任务
    key.mv_data = &task_id;
    key.mv_size = sizeof(task_id);

    rc = mdb_get(txn, tasks_db, &key, &data);
    if (rc == 0) {
        task_t *task = data.mv_data;
        strcpy(task->status, STATUS_FAILED);
        task->completed_at = time(NULL);
        task->retry_count++;
        if (error) {
            strncpy(task->result, error, sizeof(task->result) - 1);
        }

        mdb_put(txn, tasks_db, &key, &data, 0);
    }

    // 从运行中队列移除
    // （简化实现）

    // 添加到失败队列
    snprintf(failed_key, sizeof(failed_key), "%020lu", task_id);
    MDB_val failed_key_val = { .mv_data = failed_key, .mv_size = strlen(failed_key) };
    mdb_put(txn, failed_db, &failed_key_val, &key, 0);

    pthread_mutex_unlock(&tq_mutex);

    return mdb_txn_commit(txn);
}

// 获取统计信息
int taskqueue_get_stats(taskqueue_t *tq, queue_stats_t *stats) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_stat st;
    int rc;

    if (!tq || !stats) return -1;

    memset(stats, 0, sizeof(*stats));

    rc = mdb_txn_begin(tq->env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) return rc;

    // 统计各队列
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

// 列出任务
int taskqueue_list(taskqueue_t *tq, const char *status,
                   void (*callback)(task_t*, void*), void *arg) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_cursor *cursor;
    MDB_val key, data;
    int rc;

    if (!tq || !callback) return -1;

    rc = mdb_txn_begin(tq->env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) return rc;

    // 根据状态选择数据库
    if (strcmp(status, STATUS_PENDING) == 0) {
        mdb_dbi_open(txn, DB_PENDING, 0, &dbi);
    } else if (strcmp(status, STATUS_RUNNING) == 0) {
        mdb_dbi_open(txn, DB_RUNNING, 0, &dbi);
    } else if (strcmp(status, STATUS_COMPLETED) == 0) {
        mdb_dbi_open(txn, DB_COMPLETED, 0, &dbi);
    } else if (strcmp(status, STATUS_FAILED) == 0) {
        mdb_dbi_open(txn, DB_FAILED, 0, &dbi);
    } else {
        mdb_dbi_open(txn, DB_TASKS, 0, &dbi);
    }

    mdb_cursor_open(txn, dbi, &cursor);

    rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    while (rc == 0) {
        task_t task;

        if (strcmp(status, STATUS_PENDING) == 0 ||
            strcmp(status, STATUS_RUNNING) == 0 ||
            strcmp(status, STATUS_COMPLETED) == 0 ||
            strcmp(status, STATUS_FAILED) == 0) {
            // 这些数据库存储的是 task_id 引用
            uint64_t *task_id_ptr = key.mv_data;
            MDB_val task_key = { .mv_data = task_id_ptr, .mv_size = sizeof(uint64_t) };
            MDB_val task_data;

            if (mdb_get(txn, mdb_dbi_open(txn, DB_TASKS, 0, &dbi), &task_key, &task_data) == 0) {
                memcpy(&task, task_data.mv_data, sizeof(task_t));
                callback(&task, arg);
            }
        } else {
            // 直接从 tasks 数据库读取
            memcpy(&task, data.mv_data, sizeof(task_t));
            callback(&task, arg);
        }

        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);

    return 0;
}
