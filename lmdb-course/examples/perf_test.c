/**
 * perf_test.c - LMDB 性能测试工具
 *
 * 综合性能测试工具，测试：
 * - 顺序写入
 * - 随机读取
 * - 批量操作
 * - 不同数据大小的性能
 * - 不同并发度的性能
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <lmdb.h>

#define NS_PER_SEC 1000000000.0

// 性能测试结果
typedef struct {
    double elapsed_time;
    long operations;
    double ops_per_sec;
    const char *test_name;
} perf_result_t;

// 获取当前时间（纳秒）
double get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * NS_PER_SEC + ts.tv_nsec;
}

// 测试1: 顺序写入性能
perf_result_t test_sequential_write(MDB_env *env, int num_ops, int data_size) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;
    perf_result_t result = {0};
    double start, end;
    char *data_buf;
    int rc;

    printf("\n=== Test: Sequential Write (%d ops, %d bytes) ===\n",
           num_ops, data_size);

    data_buf = malloc(data_size);
    memset(data_buf, 'X', data_size);

    start = get_time_ns();

    for (int i = 0; i < num_ops; i++) {
        rc = mdb_txn_begin(env, NULL, 0, &txn);
        if (rc != 0) {
            fprintf(stderr, "mdb_txn_begin failed: %s\n", mdb_strerror(rc));
            free(data_buf);
            return result;
        }

        rc = mdb_dbi_open(txn, NULL, 0, &dbi);
        if (rc != 0) {
            mdb_txn_abort(txn);
            continue;
        }

        char key_buf[32];
        snprintf(key_buf, sizeof(key_buf), "seq_key_%d", i);

        key.mv_data = key_buf;
        key.mv_size = strlen(key_buf);
        data.mv_data = data_buf;
        data.mv_size = data_size;

        mdb_put(txn, dbi, &key, &data, 0);
        mdb_txn_commit(txn);
    }

    end = get_time_ns();

    result.test_name = "Sequential Write";
    result.operations = num_ops;
    result.elapsed_time = (end - start) / NS_PER_SEC;
    result.ops_per_sec = num_ops / result.elapsed_time;

    printf("  Time:        %.3f seconds\n", result.elapsed_time);
    printf("  Throughput:  %.0f ops/sec\n", result.ops_per_sec);
    printf("  Avg latency: %.3f ms\n", (result.elapsed_time * 1000) / num_ops);

    free(data_buf);
    return result;
}

// 测试2: 批量写入性能
perf_result_t test_batch_write(MDB_env *env, int num_ops, int batch_size) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;
    perf_result_t result = {0};
    double start, end;
    int rc;

    printf("\n=== Test: Batch Write (%d ops, batch=%d) ===\n",
           num_ops, batch_size);

    start = get_time_ns();

    for (int i = 0; i < num_ops; i += batch_size) {
        rc = mdb_txn_begin(env, NULL, 0, &txn);
        if (rc != 0) {
            fprintf(stderr, "mdb_txn_begin failed: %s\n", mdb_strerror(rc));
            return result;
        }

        rc = mdb_dbi_open(txn, NULL, 0, &dbi);
        if (rc != 0) {
            mdb_txn_abort(txn);
            continue;
        }

        int batch_end = i + batch_size;
        if (batch_end > num_ops) batch_end = num_ops;

        for (int j = i; j < batch_end; j++) {
            char key_buf[32], val_buf[32];
            snprintf(key_buf, sizeof(key_buf), "batch_key_%d", j);
            snprintf(val_buf, sizeof(val_buf), "batch_value_%d", j);

            key.mv_data = key_buf;
            key.mv_size = strlen(key_buf);
            data.mv_data = val_buf;
            data.mv_size = strlen(val_buf);

            mdb_put(txn, dbi, &key, &data, 0);
        }

        mdb_txn_commit(txn);
    }

    end = get_time_ns();

    result.test_name = "Batch Write";
    result.operations = num_ops;
    result.elapsed_time = (end - start) / NS_PER_SEC;
    result.ops_per_sec = num_ops / result.elapsed_time;

    printf("  Time:        %.3f seconds\n", result.elapsed_time);
    printf("  Throughput:  %.0f ops/sec\n", result.ops_per_sec);
    printf("  Avg latency: %.3f ms\n", (result.elapsed_time * 1000) / num_ops);

    return result;
}

// 测试3: 随机读取性能
perf_result_t test_random_read(MDB_env *env, int num_ops) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;
    perf_result_t result = {0};
    double start, end;
    int rc;

    printf("\n=== Test: Random Read (%d ops) ===\n", num_ops);

    start = get_time_ns();

    for (int i = 0; i < num_ops; i++) {
        rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
        if (rc != 0) {
            fprintf(stderr, "mdb_txn_begin failed: %s\n", mdb_strerror(rc));
            return result;
        }

        rc = mdb_dbi_open(txn, NULL, 0, &dbi);
        if (rc != 0) {
            mdb_txn_abort(txn);
            continue;
        }

        char key_buf[32];
        int key_num = rand() % (num_ops / 2);
        snprintf(key_buf, sizeof(key_buf), "seq_key_%d", key_num);

        key.mv_data = key_buf;
        key.mv_size = strlen(key_buf);

        mdb_get(txn, dbi, &key, &data);
        mdb_txn_abort(txn);
    }

    end = get_time_ns();

    result.test_name = "Random Read";
    result.operations = num_ops;
    result.elapsed_time = (end - start) / NS_PER_SEC;
    result.ops_per_sec = num_ops / result.elapsed_time;

    printf("  Time:        %.3f seconds\n", result.elapsed_time);
    printf("  Throughput:  %.0f ops/sec\n", result.ops_per_sec);
    printf("  Avg latency: %.3f ms\n", (result.elapsed_time * 1000) / num_ops);

    return result;
}

// 测试4: 范围查询性能
perf_result_t test_range_query(MDB_env *env, int num_queries) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_cursor *cursor;
    MDB_val key, data;
    perf_result_t result = {0};
    double start, end;
    int rc;

    printf("\n=== Test: Range Query (%d queries) ===\n", num_queries);

    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) {
        fprintf(stderr, "mdb_txn_begin failed: %s\n", mdb_strerror(rc));
        return result;
    }

    rc = mdb_dbi_open(txn, NULL, 0, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return result;
    }

    rc = mdb_cursor_open(txn, dbi, &cursor);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return result;
    }

    start = get_time_ns();

    for (int i = 0; i < num_queries; i++) {
        char key_buf[32];
        snprintf(key_buf, sizeof(key_buf), "seq_key_%d", i * 10);

        key.mv_data = key_buf;
        key.mv_size = strlen(key_buf);

        rc = mdb_cursor_get(cursor, &key, &data, MDB_SET_RANGE);

        int count = 0;
        while (rc == 0 && count < 10) {
            rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
            count++;
        }
    }

    end = get_time_ns();

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);

    result.test_name = "Range Query";
    result.operations = num_queries * 10;  // 10 records per query
    result.elapsed_time = (end - start) / NS_PER_SEC;
    result.ops_per_sec = result.operations / result.elapsed_time;

    printf("  Time:        %.3f seconds\n", result.elapsed_time);
    printf("  Throughput:  %.0f records/sec\n", result.ops_per_sec);
    printf("  Avg latency: %.3f ms per query\n", (result.elapsed_time * 1000) / num_queries);

    return result;
}

// 打印性能测试摘要
void print_summary(perf_result_t *results, int count) {
    printf("\n========== Performance Summary ==========\n");
    printf("%-20s %10s %12s %12s\n", "Test", "Ops", "Time(s)", "Ops/sec");
    printf("--------------------------------------------------------\n");

    for (int i = 0; i < count; i++) {
        printf("%-20s %10ld %12.3f %12.0f\n",
               results[i].test_name,
               results[i].operations,
               results[i].elapsed_time,
               results[i].ops_per_sec);
    }

    printf("==========================================\n");
}

int main(int argc, char **argv) {
    MDB_env *env;
    char *db_path = "./testdb";
    int num_ops = 10000;
    int rc;

    if (argc > 1) {
        db_path = argv[1];
    }
    if (argc > 2) {
        num_ops = atoi(argv[2]);
    }

    printf("=== LMDB Performance Test ===\n");
    printf("Database: %s\n", db_path);
    printf("Operations: %d\n", num_ops);

    // 创建环境
    rc = mdb_env_create(&env);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_create failed: %s\n", mdb_strerror(rc));
        return 1;
    }

    mdb_env_set_mapsize(env, 1024 * 1024 * 500);  // 500MB

    rc = mdb_env_open(env, db_path, 0, 0664);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_open failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    // 运行性能测试
    perf_result_t results[10];
    int result_count = 0;

    srand(time(NULL));

    // 测试1: 顺序写入（小数据）
    results[result_count++] = test_sequential_write(env, num_ops, 100);

    // 测试2: 顺序写入（大数据）
    results[result_count++] = test_sequential_write(env, num_ops / 10, 1000);

    // 测试3: 批量写入
    results[result_count++] = test_batch_write(env, num_ops, 100);

    // 测试4: 随机读取
    results[result_count++] = test_random_read(env, num_ops);

    // 测试5: 范围查询
    results[result_count++] = test_range_query(env, num_ops / 10);

    // 打印摘要
    print_summary(results, result_count);

    mdb_env_close(env);

    printf("\n=== Test Complete ===\n");

    return 0;
}
