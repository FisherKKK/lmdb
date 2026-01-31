/**
 * first_example.c - Day 1: 第一个 LMDB 程序
 *
 * 这个程序演示了 LMDB 的基本用法：
 * 1. 创建环境
 * 2. 打开数据库
 * 3. 写入数据
 * 4. 读取数据
 * 5. 清理资源
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lmdb.h>

int main() {
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;
    int rc;

    printf("=== LMDB First Example ===\n\n");

    // 1. 创建环境
    rc = mdb_env_create(&env);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_create failed: %s\n", mdb_strerror(rc));
        return 1;
    }
    printf("[1/8] Environment created\n");

    // 2. 设置映射大小（重要！）
    mdb_env_set_mapsize(env, 1024 * 1024 * 100);  // 100MB
    printf("[2/8] Map size set to 100MB\n");

    // 3. 设置最大数据库数
    mdb_env_set_maxdbs(env, 2);
    printf("[3/8] Max databases set to 2\n");

    // 4. 打开环境
    rc = mdb_env_open(env, "./testdb", 0, 0664);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_open failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }
    printf("[4/8] Environment opened at './testdb'\n");

    // 5. 开始事务
    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc != 0) {
        fprintf(stderr, "mdb_txn_begin failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }
    printf("[5/8] Write transaction started\n");

    // 6. 打开数据库
    rc = mdb_dbi_open(txn, NULL, 0, &dbi);
    if (rc != 0) {
        fprintf(stderr, "mdb_dbi_open failed: %s\n", mdb_strerror(rc));
        mdb_txn_abort(txn);
        mdb_env_close(env);
        return 1;
    }
    printf("[6/8] Database opened\n");

    // 7. 插入数据
    key.mv_data = "hello";
    key.mv_size = 5;
    data.mv_data = "world";
    data.mv_size = 5;

    rc = mdb_put(txn, dbi, &key, &data, 0);
    if (rc != 0) {
        fprintf(stderr, "mdb_put failed: %s\n", mdb_strerror(rc));
        mdb_txn_abort(txn);
        mdb_env_close(env);
        return 1;
    }

    printf("[7/8] Data inserted: hello -> world\n");

    // 8. 提交事务
    rc = mdb_txn_commit(txn);
    if (rc != 0) {
        fprintf(stderr, "mdb_txn_commit failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }
    printf("[8/8] Transaction committed\n\n");

    // 验证：读取数据
    printf("--- Verifying data ---\n");
    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) {
        fprintf(stderr, "mdb_txn_begin (read) failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    key.mv_data = "hello";
    key.mv_size = 5;

    rc = mdb_get(txn, dbi, &key, &data);
    if (rc == 0) {
        printf("Retrieved: %.*s -> %.*s\n",
               (int)key.mv_size, (char *)key.mv_data,
               (int)data.mv_size, (char *)data.mv_data);
    } else if (rc == MDB_NOTFOUND) {
        printf("Key not found\n");
    } else {
        fprintf(stderr, "mdb_get failed: %s\n", mdb_strerror(rc));
    }

    mdb_txn_abort(txn);

    // 清理
    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
    printf("\n=== Cleanup complete ===\n");

    return 0;
}
