/**
 * concurrent_demo.c - 多线程并发演示
 *
 * 演示 LMDB 的并发读写能力：
 * - 多个读事务可以同时进行
 * - 写事务是互斥的
 * - MVCC 保证读写不阻塞
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <lmdb.h>

#define NUM_READERS 5
#define NUM_WRITERS 2
#define OPERATIONS_PER_THREAD 100

typedef struct {
    MDB_env *env;
    int thread_id;
    int is_writer;
    int operations;
} thread_data_t;

// 读线程工作函数
void *reader_thread(void *arg) {
    thread_data_t *ctx = (thread_data_t *)arg;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, value;
    int rc;
    int successful_reads = 0;

    printf("[Reader %d] Started\n", ctx->thread_id);

    for (int i = 0; i < ctx->operations; i++) {
        rc = mdb_txn_begin(ctx->env, NULL, MDB_RDONLY, &txn);
        if (rc != 0) {
            fprintf(stderr, "[Reader %d] Failed to begin txn: %s\n",
                    ctx->thread_id, mdb_strerror(rc));
            continue;
        }

        rc = mdb_dbi_open(txn, NULL, 0, &dbi);
        if (rc != 0) {
            mdb_txn_abort(txn);
            continue;
        }

        // 读取一个随机键
        char key_buf[32];
        snprintf(key_buf, sizeof(key_buf), "key_%d", rand() % 100);
        key.mv_data = key_buf;
        key.mv_size = strlen(key_buf);

        rc = mdb_get(txn, dbi, &key, &value);
        if (rc == 0) {
            successful_reads++;
        }

        mdb_txn_abort(txn);

        // 短暂休眠，模拟实际使用
        usleep(1000);  // 1ms
    }

    printf("[Reader %d] Completed %d successful reads\n",
           ctx->thread_id, successful_reads);
    return NULL;
}

// 写线程工作函数
void *writer_thread(void *arg) {
    thread_data_t *ctx = (thread_data_t *)arg;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, value;
    int rc;
    int successful_writes = 0;

    printf("[Writer %d] Started\n", ctx->thread_id);

    for (int i = 0; i < ctx->operations; i++) {
        rc = mdb_txn_begin(ctx->env, NULL, 0, &txn);
        if (rc != 0) {
            if (rc == MDB_TXN_FULL) {
                // 另一个写事务正在进行，稍后重试
                usleep(1000);  // 1ms
                i--;  // 重试此操作
                continue;
            }
            fprintf(stderr, "[Writer %d] Failed to begin txn: %s\n",
                    ctx->thread_id, mdb_strerror(rc));
            continue;
        }

        rc = mdb_dbi_open(txn, NULL, 0, &dbi);
        if (rc != 0) {
            mdb_txn_abort(txn);
            continue;
        }

        // 写入一个键值对
        char key_buf[32], val_buf[32];
        int key_num = rand() % 100;
        snprintf(key_buf, sizeof(key_buf), "key_%d", key_num);
        snprintf(val_buf, sizeof(val_buf), "value_%ld_%d",
                 (long)time(NULL), key_num);

        key.mv_data = key_buf;
        key.mv_size = strlen(key_buf);
        value.mv_data = val_buf;
        value.mv_size = strlen(val_buf);

        rc = mdb_put(txn, dbi, &key, &value, 0);
        if (rc == 0) {
            successful_writes++;
        }

        rc = mdb_txn_commit(txn);
        if (rc != 0) {
            fprintf(stderr, "[Writer %d] Failed to commit: %s\n",
                    ctx->thread_id, mdb_strerror(rc));
        }

        // 短暂休眠
        usleep(5000);  // 5ms
    }

    printf("[Writer %d] Completed %d successful writes\n",
           ctx->thread_id, successful_writes);
    return NULL;
}

int main() {
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;
    pthread_t readers[NUM_READERS];
    pthread_t writers[NUM_WRITERS];
    thread_data_t reader_data[NUM_READERS];
    thread_data_t writer_data[NUM_WRITERS];
    int rc;

    printf("=== LMDB Concurrent Access Demo ===\n\n");

    // 创建并初始化环境
    rc = mdb_env_create(&env);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_create failed: %s\n", mdb_strerror(rc));
        return 1;
    }

    mdb_env_set_mapsize(env, 1024 * 1024 * 100);  // 100MB
    mdb_env_set_maxreaders(env, NUM_READERS + 10);

    rc = mdb_env_open(env, "./testdb", 0, 0664);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_open failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    // 初始化一些测试数据
    printf("Initializing test data...\n");
    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc != 0) {
        fprintf(stderr, "mdb_txn_begin failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    rc = mdb_dbi_open(txn, NULL, 0, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        mdb_env_close(env);
        return 1;
    }

    for (int i = 0; i < 50; i++) {
        char key_buf[32], val_buf[32];
        snprintf(key_buf, sizeof(key_buf), "key_%d", i);
        snprintf(val_buf, sizeof(val_buf), "initial_value_%d", i);

        key.mv_data = key_buf;
        key.mv_size = strlen(key_buf);
        data.mv_data = val_buf;
        data.mv_size = strlen(val_buf);

        mdb_put(txn, dbi, &key, &data, 0);
    }

    mdb_txn_commit(txn);
    printf("Test data initialized.\n\n");

    // 启动读线程
    printf("Starting %d reader threads...\n", NUM_READERS);
    for (int i = 0; i < NUM_READERS; i++) {
        reader_data[i].env = env;
        reader_data[i].thread_id = i;
        reader_data[i].is_writer = 0;
        reader_data[i].operations = OPERATIONS_PER_THREAD;

        rc = pthread_create(&readers[i], NULL, reader_thread, &reader_data[i]);
        if (rc != 0) {
            perror("pthread_create (reader)");
        }
    }

    // 启动写线程
    printf("Starting %d writer threads...\n\n", NUM_WRITERS);
    for (int i = 0; i < NUM_WRITERS; i++) {
        writer_data[i].env = env;
        writer_data[i].thread_id = i;
        writer_data[i].is_writer = 1;
        writer_data[i].operations = OPERATIONS_PER_THREAD;

        rc = pthread_create(&writers[i], NULL, writer_thread, &writer_data[i]);
        if (rc != 0) {
            perror("pthread_create (writer)");
        }
    }

    printf("All threads started. Waiting for completion...\n\n");

    // 等待所有读线程完成
    for (int i = 0; i < NUM_READERS; i++) {
        pthread_join(readers[i], NULL);
    }

    // 等待所有写线程完成
    for (int i = 0; i < NUM_WRITERS; i++) {
        pthread_join(writers[i], NULL);
    }

    printf("\n=== All threads completed ===\n");

    // 统计最终数据
    MDB_stat stat;
    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc == 0) {
        mdb_stat(txn, dbi, &stat);
        printf("\nFinal database statistics:\n");
        printf("  Total entries: %zu\n", (size_t)stat.ms_entries);
        printf("  Tree depth: %u\n", stat.ms_depth);
        mdb_txn_abort(txn);
    }

    mdb_dbi_close(env, dbi);
    mdb_env_close(env);

    printf("\n=== Demo Complete ===\n");
    printf("\nKey observations:\n");
    printf("- Multiple readers can run simultaneously without blocking\n");
    printf("- Only one writer can be active at a time\n");
    printf("- Readers never block writers and vice versa (MVCC)\n");
    printf("- Writers retry with MDB_TXN_FULL if another write is in progress\n");

    return 0;
}
