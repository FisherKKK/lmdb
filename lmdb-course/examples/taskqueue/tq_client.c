/**
 * tq_client.c - Task Queue Command Line Client
 *
 * 用于创建和管理任务的命令行工具
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "taskqueue.h"

void print_stats(taskqueue_t *tq) {
    queue_stats_t stats;

    if (taskqueue_get_stats(tq, &stats) == 0) {
        printf("\n========== Queue Statistics ==========\n");
        printf("Total Tasks:    %lu\n", stats.total_tasks);
        printf("Pending:        %lu\n", stats.pending_tasks);
        printf("Running:        %lu\n", stats.running_tasks);
        printf("Completed:      %lu\n", stats.completed_tasks);
        printf("Failed:         %lu\n", stats.failed_tasks);
        printf("======================================\n\n");
    }
}

void task_print_callback(task_t *task, void *arg) {
    printf("Task ID: %lu\n", task->task_id);
    printf("  Name:    %s\n", task->name);
    printf("  Status:  %s\n", task->status);
    printf("  Priority: %d\n", task->priority);
    printf("  Created: %s", ctime(&task->created_at));
    if (strcmp(task->status, STATUS_COMPLETED) == 0 ||
        strcmp(task->status, STATUS_FAILED) == 0) {
        printf("  Completed: %s", ctime(&task->completed_at));
    }
    printf("\n");
}

int main(int argc, char **argv) {
    taskqueue_t tq;
    char *db_path = "./taskqueue_db";
    int rc;

    if (argc < 2) {
        printf("Usage: %s <command> [options]\n\n", argv[0]);
        printf("Commands:\n");
        printf("  create <name> [-p priority] [-d data]    Create a new task\n");
        printf("  stats                                   Show queue statistics\n");
        printf("  list [pending|running|completed|failed] List tasks\n");
        printf("  get <task_id>                           Get task details\n");
        printf("\nExamples:\n");
        printf("  %s create \"Process data\" -p 5 -d '{\"file\":\"data.csv\"}'\n", argv[0]);
        printf("  %s stats\n", argv[0]);
        printf("  %s list pending\n", argv[0]);
        return 1;
    }

    char *command = argv[1];

    // 初始化队列环境
    rc = taskqueue_init(&tq, db_path, 1024 * 1024 * 100);  // 100MB
    if (rc != 0) {
        fprintf(stderr, "Failed to initialize task queue: %d\n", rc);
        return 1;
    }

    // 处理命令
    if (strcmp(command, "create") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s create <name> [-p priority] [-d data]\n", argv[0]);
            taskqueue_close(&tq);
            return 1;
        }

        task_t task = {0};
        strncpy(task.name, argv[2], sizeof(task.name) - 1);
        task.priority = 5;  // 默认优先级

        // 解析选项
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
                task.priority = atoi(argv[++i]);
            } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
                strncpy(task.data, argv[++i], sizeof(task.data) - 1);
            }
        }

        rc = task_create(&tq, &task);
        if (rc == 0) {
            printf("Task created successfully: ID=%lu, Name=%s, Priority=%d\n",
                   task.task_id, task.name, task.priority);
        } else {
            fprintf(stderr, "Failed to create task: %s\n", mdb_strerror(rc));
        }

    } else if (strcmp(command, "stats") == 0) {
        print_stats(&tq);

    } else if (strcmp(command, "list") == 0) {
        char *status = argv[2] ? argv[2] : STATUS_PENDING;
        printf("Listing %s tasks:\n\n", status);
        taskqueue_list(&tq, status, task_print_callback, NULL);

    } else if (strcmp(command, "get") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s get <task_id>\n", argv[0]);
            taskqueue_close(&tq);
            return 1;
        }

        uint64_t task_id = atol(argv[2]);
        task_t task;

        rc = task_get(&tq, task_id, &task);
        if (rc == 0) {
            task_print_callback(&task, NULL);
        } else {
            fprintf(stderr, "Task not found: %lu\n", task_id);
        }

    } else {
        fprintf(stderr, "Unknown command: %s\n", command);
        taskqueue_close(&tq);
        return 1;
    }

    taskqueue_close(&tq);
    return 0;
}
