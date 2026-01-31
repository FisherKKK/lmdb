/**
 * tq_worker.c - Task Queue Worker Process
 *
 * 工作进程，从队列中获取任务并执行
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include "taskqueue.h"

static volatile int running = 1;

void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

// 模拟任务处理
int process_task(task_t *task) {
    printf("Processing task %lu: %s\n", task->task_id, task->name);
    printf("  Data: %s\n", task->data);
    printf("  Priority: %d\n", task->priority);

    // 模拟处理时间（1-5秒）
    int duration = 1 + rand() % 5;
    sleep(duration);

    // 模拟成功率（90%）
    if (rand() % 100 < 90) {
        return 0;
    } else {
        return -1;
    }
}

void worker_loop(taskqueue_t *tq, const char *worker_id) {
    int task_count = 0;

    printf("[%s] Worker started\n", worker_id);

    while (running) {
        task_t task;

        int rc = task_acquire(tq, worker_id, &task);
        if (rc == 0) {
            task_count++;

            // 处理任务
            int result = process_task(&task);

            if (result == 0) {
                char result_msg[256];
                snprintf(result_msg, sizeof(result_msg),
                         "Completed successfully in %ld seconds",
                         time(NULL) - task.started_at);

                task_complete(tq, task.task_id, result_msg);
                printf("[%s] Task %lu completed\n", worker_id, task.task_id);
            } else {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg),
                         "Failed after %ld seconds",
                         time(NULL) - task.started_at);

                task_fail(tq, task.task_id, error_msg);
                printf("[%s] Task %lu failed\n", worker_id, task.task_id);
            }
        } else if (rc == MDB_NOTFOUND) {
            // 队列空，等待一会
            usleep(500000);  // 500ms
        } else {
            fprintf(stderr, "[%s] Error acquiring task: %s\n",
                   worker_id, mdb_strerror(rc));
            usleep(1000000);  // 1秒
        }
    }

    printf("[%s] Worker stopped (processed %d tasks)\n", worker_id, task_count);
}

int main(int argc, char **argv) {
    taskqueue_t tq;
    char *db_path = "./taskqueue_db";
    char worker_id[64];
    int rc;

    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 生成 worker ID
    snprintf(worker_id, sizeof(worker_id), "worker-%d", getpid());

    printf("========================================\n");
    printf("    Task Queue Worker v1.0          \n");
    printf("========================================\n");
    printf("Worker ID: %s\n", worker_id);
    printf("Database: %s\n\n", db_path);

    // 初始化队列环境
    rc = taskqueue_init(&tq, db_path, 1024 * 1024 * 100);
    if (rc != 0) {
        fprintf(stderr, "Failed to initialize task queue: %d\n", rc);
        return 1;
    }

    printf("Press Ctrl+C to stop\n\n");

    // 运行工作循环
    worker_loop(&tq, worker_id);

    // 清理
    taskqueue_close(&tq);

    printf("\nWorker shutdown complete\n");
    return 0;
}
