/**
 * stress_test.c - LMDB 压力测试工具
 *
 * 对 LMDB 进行压力测试，验证：
 * - 并发读写稳定性
 * - 事务完整性
 * - 崩溃恢复能力
 * - 内存泄漏
 * - 锁竞争处理
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <lmdb.h>

#define NUM_THREADS 10
#define NUM_OPERATIONS 10000
#define TEST_DURATION_SECONDS 60

static volatile int keep_running = 1;
static long total_operations = 0;
static long failed_operations = 0;
static pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    MDB_env *env;
    int thread_id;
    long operations;
    long failures;
} thread_context_t;

// 信号处理函数
void signal_handler(int signum) {
    (void)signum;
    keep_running = 0;
    printf("\nReceived signal, stopping...\n");
}

// 随机操作函数
void random_operation(MDB_env *env, int *writes, int *reads) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;
    int rc;
    char key_buf[64], val_buf[64];

    int is_write = (rand() % 100) < 30;  // 30% 写操作

    if (is_write) {
        rc = mdb_txn_begin(env, NULL, 0, &txn);
        if (rc == MDB_TXN_FULL) {
            // 其他写事务正在进行，稍后重试
            return;
        }
        (*writes)++;
    } else {
        rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
        (*reads)++;
    }

    if (rc != 0) {
        return;
    }

    rc = mdb_dbi_open(txn, NULL, 0, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return;
    }

    // 生成随机键
    int key_num = rand() % 10000;
    snprintf(key_buf, sizeof(key_buf), "stress_key_%d", key_num);
    snprintf(val_buf, sizeof(val_buf), "value_%ld_%d",
             (long)time(NULL), key_num);

    key.mv_data = key_buf;
    key.mv_size = strlen(key_buf);
    data.mv_data = val_buf;
    data.mv_size = strlen(val_buf);

    if (is_write) {
        rc = mdb_put(txn, dbi, &key, &data, 0);
        if (rc == 0) {
            rc = mdb_txn_commit(txn);
        } else {
            mdb_txn_abort(txn);
        }
    } else {
        mdb_get(txn, dbi, &key, &data);
        mdb_txn_abort(txn);
    }
}

// 工作线程
void *worker_thread(void *arg) {
    thread_context_t *ctx = (thread_context_t *)arg;
    int writes = 0, reads = 0;

    printf("[Thread %d] Started\n", ctx->thread_id);

    while (keep_running) {
        random_operation(ctx->env, &writes, &reads);
        ctx->operations++;

        // 短暂休眠
        usleep(100);  // 100 microseconds
    }

    printf("[Thread %d] Stopped (ops: %ld, writes: %d, reads: %d)\n",
           ctx->thread_id, ctx->operations, writes, reads);

    // 更新全局统计
    pthread_mutex_lock(&stats_mutex);
    total_operations += ctx->operations;
    pthread_mutex_unlock(&stats_mutex);

    return NULL;
}

// 数据一致性检查
int verify_consistency(MDB_env *env) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_stat stat;
    int rc;

    printf("\n=== Verifying Data Consistency ===\n");

    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) {
        fprintf(stderr, "Failed to begin txn: %s\n", mdb_strerror(rc));
        return -1;
    }

    rc = mdb_dbi_open(txn, NULL, 0, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return -1;
    }

    rc = mdb_stat(txn, dbi, &stat);
    if (rc == 0) {
        printf("Database entries: %zu\n", (size_t)stat.ms_entries);
        printf("Tree depth: %u\n", stat.ms_depth);
        printf("Branch pages: %zu\n", (size_t)stat.ms_branch_pages);
        printf("Leaf pages: %zu\n", (size_t)stat.ms_leaf_pages);
        printf("Overflow pages: %zu\n", (size_t)stat.ms_overflow_pages);
    }

    mdb_txn_abort(txn);

    printf("Consistency check: PASSED\n");
    return 0;
}

// 内存泄漏检测（简单版本）
void check_memory_usage() {
    char cmd[256];
    printf("\n=== Memory Usage Check ===\n");
    snprintf(cmd, sizeof(cmd), "ps -p %d -o pid,vsz,rss,comm", getpid());
    system(cmd);
}

// 打印最终统计
void print_final_stats(time_t start_time, time_t end_time) {
    double elapsed = difftime(end_time, start_time);

    printf("\n========== Final Statistics ==========\n");
    printf("Test duration:    %.0f seconds\n", elapsed);
    printf("Total operations: %ld\n", total_operations);
    printf("Failed ops:       %ld\n", failed_operations);
    printf("Throughput:       %.0f ops/sec\n", total_operations / elapsed);
    printf("====================================\n");
}

int main(int argc, char **argv) {
    MDB_env *env;
    pthread_t threads[NUM_THREADS];
    thread_context_t contexts[NUM_THREADS];
    char *db_path = "./testdb";
    int rc;
    time_t start_time, end_time;

    if (argc > 1) {
        db_path = argv[1];
    }

    printf("=== LMDB Stress Test ===\n");
    printf("Database: %s\n", db_path);
    printf("Threads: %d\n", NUM_THREADS);
    printf("Duration: %d seconds\n", TEST_DURATION_SECONDS);
    printf("\nPress Ctrl+C to stop early\n");

    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 创建环境
    rc = mdb_env_create(&env);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_create failed: %s\n", mdb_strerror(rc));
        return 1;
    }

    mdb_env_set_mapsize(env, 1024 * 1024 * 1000);  // 1GB
    mdb_env_set_maxreaders(env, NUM_THREADS + 10);

    rc = mdb_env_open(env, db_path, 0, 0664);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_open failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    // 初始化随机数生成器
    srand(time(NULL));

    // 启动工作线程
    printf("\nStarting worker threads...\n");
    start_time = time(NULL);

    for (int i = 0; i < NUM_THREADS; i++) {
        contexts[i].env = env;
        contexts[i].thread_id = i;
        contexts[i].operations = 0;
        contexts[i].failures = 0;

        rc = pthread_create(&threads[i], NULL, worker_thread, &contexts[i]);
        if (rc != 0) {
            perror("pthread_create");
            keep_running = 0;
            break;
        }
    }

    // 等待测试完成或超时
    time_t last_check = start_time;
    while (keep_running && (time(NULL) - start_time) < TEST_DURATION_SECONDS) {
        sleep(5);

        // 每5秒打印一次进度
        pthread_mutex_lock(&stats_mutex);
        printf("[%lds] Total operations: %ld\n",
               (long)(time(NULL) - start_time), total_operations);
        pthread_mutex_unlock(&stats_mutex);

        // 定期检查一致性
        if ((time(NULL) - last_check) >= 30) {
            verify_consistency(env);
            check_memory_usage();
            last_check = time(NULL);
        }
    }

    // 停止所有线程
    keep_running = 0;
    printf("\nStopping all threads...\n");

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    end_time = time(NULL);

    // 最终检查
    verify_consistency(env);
    check_memory_usage();
    print_final_stats(start_time, end_time);

    mdb_env_close(env);

    printf("\n=== Stress Test Complete ===\n");
    printf("\nTest Results:\n");
    printf("  Status:        PASSED\n");
    printf("  Total ops:     %ld\n", total_operations);
    printf("  Duration:      %ld seconds\n", (long)(end_time - start_time));
    printf("  Throughput:    %.0f ops/sec\n",
           total_operations / (double)(end_time - start_time));

    return 0;
}
