/**
 * cursor_demo.c - Day 9: 游标使用演示
 *
 * 演示游标的基本用法和遍历操作
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lmdb.h>

int main() {
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_cursor *cursor;
    MDB_val key, data;
    int rc;

    printf("=== LMDB Cursor Demo ===\n\n");

    // 创建并打开环境
    rc = mdb_env_create(&env);
    if (rc) {
        fprintf(stderr, "mdb_env_create failed: %s\n", mdb_strerror(rc));
        return 1;
    }

    mdb_env_set_mapsize(env, 1024 * 1024 * 100);
    rc = mdb_env_open(env, "./testdb", 0, 0664);
    if (rc) {
        fprintf(stderr, "mdb_env_open failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    // 写入测试数据
    printf("Step 1: Writing test data...\n");
    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc) {
        fprintf(stderr, "mdb_txn_begin failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    rc = mdb_dbi_open(txn, NULL, 0, &dbi);
    if (rc) {
        fprintf(stderr, "mdb_dbi_open failed: %s\n", mdb_strerror(rc));
        mdb_txn_abort(txn);
        mdb_env_close(env);
        return 1;
    }

    // 插入一些测试数据
    const char *test_keys[] = {"apple", "banana", "cherry", "date", "elderberry"};
    const char *test_values[] = {"red", "yellow", "red", "brown", "purple"};

    for (int i = 0; i < 5; i++) {
        key.mv_data = (void *)test_keys[i];
        key.mv_size = strlen(test_keys[i]);
        data.mv_data = (void *)test_values[i];
        data.mv_size = strlen(test_values[i]);

        rc = mdb_put(txn, dbi, &key, &data, 0);
        if (rc) {
            fprintf(stderr, "mdb_put failed: %s\n", mdb_strerror(rc));
        } else {
            printf("  Inserted: %s -> %s\n", test_keys[i], test_values[i]);
        }
    }

    rc = mdb_txn_commit(txn);
    if (rc) {
        fprintf(stderr, "mdb_txn_commit failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    // 使用游标遍历数据
    printf("\nStep 2: Traversing with cursor...\n");

    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc) {
        fprintf(stderr, "mdb_txn_begin (read) failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    rc = mdb_cursor_open(txn, dbi, &cursor);
    if (rc) {
        fprintf(stderr, "mdb_cursor_open failed: %s\n", mdb_strerror(rc));
        mdb_txn_abort(txn);
        mdb_env_close(env);
        return 1;
    }

    // 正向遍历
    printf("\nForward traversal (MDB_NEXT):\n");
    rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    while (rc == 0) {
        printf("  %.*s -> %.*s\n",
               (int)key.mv_size, (char *)key.mv_data,
               (int)data.mv_size, (char *)data.mv_data);
        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    // 反向遍历
    printf("\nBackward traversal (MDB_PREV):\n");
    rc = mdb_cursor_get(cursor, &key, &data, MDB_LAST);
    while (rc == 0) {
        printf("  %.*s -> %.*s\n",
               (int)key.mv_size, (char *)key.mv_data,
               (int)data.mv_size, (char *)data.mv_data);
        rc = mdb_cursor_get(cursor, &key, &data, MDB_PREV);
    }

    // 查找特定键
    printf("\nSearching for 'banana' (MDB_SET):\n");
    key.mv_data = "banana";
    key.mv_size = 6;
    rc = mdb_cursor_get(cursor, &key, &data, MDB_SET);
    if (rc == 0) {
        printf("  Found: %.*s -> %.*s\n",
               (int)key.mv_size, (char *)key.mv_data,
               (int)data.mv_size, (char *)data.mv_data);
    } else {
        printf("  Not found\n");
    }

    // 范围查询
    printf("\nRange query starting from 'c' (MDB_SET_RANGE):\n");
    key.mv_data = "c";
    key.mv_size = 1;
    rc = mdb_cursor_get(cursor, &key, &data, MDB_SET_RANGE);
    while (rc == 0) {
        printf("  %.*s -> %.*s\n",
               (int)key.mv_size, (char *)key.mv_data,
               (int)data.mv_size, (char *)data.mv_data);
        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);

    // 清理
    mdb_dbi_close(env, dbi);
    mdb_env_close(env);

    printf("\n=== Demo Complete ===\n");
    return 0;
}
