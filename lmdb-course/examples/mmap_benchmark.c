/**
 * mmap_benchmark.c - Day 2: mmap 模式性能比较
 *
 * 这个程序比较默认模式和 WRITEMAP 模式的写入性能
 */

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
    rc = mdb_env_create(&env);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_create failed: %s\n", mdb_strerror(rc));
        return -1;
    }

    mdb_env_set_mapsize(env, 1024 * 1024 * 100);
    rc = mdb_env_open(env, mode_name, flags, 0664);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_open failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return -1;
    }

    clock_gettime(CLOCK_MONOTONIC, &start);

    // 写入测试
    const int COUNT = 100000;
    for (int i = 0; i < COUNT; i++) {
        rc = mdb_txn_begin(env, NULL, 0, &txn);
        if (rc != 0) {
            fprintf(stderr, "mdb_txn_begin failed: %s\n", mdb_strerror(rc));
            continue;
        }

        rc = mdb_dbi_open(txn, NULL, 0, &dbi);
        if (rc != 0) {
            fprintf(stderr, "mdb_dbi_open failed: %s\n", mdb_strerror(rc));
            mdb_txn_abort(txn);
            continue;
        }

        char buf[64];
        snprintf(buf, sizeof(buf), "key_%d", i);
        key.mv_data = buf;
        key.mv_size = strlen(buf);
        data.mv_data = "test_data_value_here";
        data.mv_size = strlen(data.mv_data);

        rc = mdb_put(txn, dbi, &key, &data, 0);
        if (rc != 0) {
            fprintf(stderr, "mdb_put failed: %s\n", mdb_strerror(rc));
        }

        rc = mdb_txn_commit(txn);
        if (rc != 0) {
            fprintf(stderr, "mdb_txn_commit failed: %s\n", mdb_strerror(rc));
        }
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
    printf("=== LMDB mmap Mode Performance Comparison ===\n");
    printf("Writing 100,000 key-value pairs...\n\n");

    printf("Testing default mode (read-only mmap)...\n");
    double t1 = benchmark_mode(0, "./testdb_default");

    printf("\nTesting WRITEMAP mode...\n");
    double t2 = benchmark_mode(MDB_WRITEMAP, "./testdb_writemap");

    if (t1 > 0 && t2 > 0) {
        printf("\n=== Results ===\n");
        if (t1 > t2) {
            printf("WRITEMAP is %.2fx faster\n", t1 / t2);
        } else {
            printf("Default mode is %.2fx faster\n", t2 / t1);
        }
    }

    return 0;
}
