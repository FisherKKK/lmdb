/**
 * nested_txn_demo.c - Day 6: 嵌套事务演示
 *
 * 演示 LMDB 嵌套事务的用法和行为
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lmdb.h>

void print_tree(MDB_txn *txn, MDB_dbi dbi) {
    MDB_cursor *cursor;
    MDB_val key, data;
    int rc;

    printf("  Current data:\n");

    rc = mdb_cursor_open(txn, dbi, &cursor);
    if (rc != 0) {
        printf("    (cannot open cursor)\n");
        return;
    }

    rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    if (rc != 0) {
        printf("    (empty)\n");
        mdb_cursor_close(cursor);
        return;
    }

    while (rc == 0) {
        printf("    %.*s -> %.*s\n",
               (int)key.mv_size, (char *)key.mv_data,
               (int)data.mv_size, (char *)data.mv_data);
        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
}

int main() {
    MDB_env *env;
    MDB_txn *parent_txn, *child_txn;
    MDB_dbi dbi;
    MDB_val key, data;
    int rc;

    printf("=== LMDB Nested Transaction Demo ===\n\n");

    // 创建环境
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

    // 开始父事务
    printf("1. Starting parent transaction...\n");
    rc = mdb_txn_begin(env, NULL, 0, &parent_txn);
    if (rc) {
        fprintf(stderr, "mdb_txn_begin failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    rc = mdb_dbi_open(parent_txn, NULL, 0, &dbi);
    if (rc) {
        fprintf(stderr, "mdb_dbi_open failed: %s\n", mdb_strerror(rc));
        mdb_txn_abort(parent_txn);
        mdb_env_close(env);
        return 1;
    }

    // 在父事务中插入数据
    printf("2. Inserting data in parent transaction...\n");
    key.mv_data = "key1";
    key.mv_size = 4;
    data.mv_data = "value1";
    data.mv_size = 6;
    mdb_put(parent_txn, dbi, &key, &data, 0);

    print_tree(parent_txn, dbi);

    // 开始子事务
    printf("\n3. Starting child transaction...\n");
    rc = mdb_txn_begin(env, parent_txn, 0, &child_txn);
    if (rc) {
        fprintf(stderr, "mdb_txn_begin (child) failed: %s\n", mdb_strerror(rc));
        mdb_txn_abort(parent_txn);
        mdb_env_close(env);
        return 1;
    }

    printf("4. Child transaction sees parent's data:\n");
    print_tree(child_txn, dbi);

    // 在子事务中插入数据
    printf("\n5. Inserting data in child transaction...\n");
    key.mv_data = "key2";
    key.mv_size = 4;
    data.mv_data = "value2";
    data.mv_size = 6;
    mdb_put(child_txn, dbi, &key, &data, 0);

    key.mv_data = "key3";
    key.mv_size = 4;
    data.mv_data = "value3";
    data.mv_size = 6;
    mdb_put(child_txn, dbi, &key, &data, 0);

    print_tree(child_txn, dbi);

    // 父事务看不到子事务的未提交数据
    printf("\n6. Parent cannot see child's uncommitted data:\n");
    print_tree(parent_txn, dbi);

    // 提交子事务
    printf("\n7. Committing child transaction...\n");
    rc = mdb_txn_commit(child_txn);
    if (rc) {
        fprintf(stderr, "mdb_txn_commit (child) failed: %s\n", mdb_strerror(rc));
        mdb_txn_abort(parent_txn);
        mdb_env_close(env);
        return 1;
    }

    printf("8. Parent now sees child's committed data:\n");
    print_tree(parent_txn, dbi);

    // 演示子事务回滚
    printf("\n9. Creating another child transaction to demonstrate abort...\n");
    rc = mdb_txn_begin(env, parent_txn, 0, &child_txn);
    if (rc) {
        fprintf(stderr, "mdb_txn_begin (child 2) failed: %s\n", mdb_strerror(rc));
        mdb_txn_abort(parent_txn);
        mdb_env_close(env);
        return 1;
    }

    key.mv_data = "key4";
    key.mv_size = 4;
    data.mv_data = "value4";
    data.mv_size = 6;
    mdb_put(child_txn, dbi, &key, &data, 0);

    printf("10. Child transaction added data:\n");
    print_tree(child_txn, dbi);

    printf("\n11. Aborting child transaction...\n");
    mdb_txn_abort(child_txn);

    printf("12. Parent does NOT see aborted child's data:\n");
    print_tree(parent_txn, dbi);

    // 提交父事务
    printf("\n13. Committing parent transaction...\n");
    rc = mdb_txn_commit(parent_txn);
    if (rc) {
        fprintf(stderr, "mdb_txn_commit (parent) failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    // 验证最终结果
    printf("\n14. Verifying final result with new transaction:\n");
    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &parent_txn);
    if (rc) {
        fprintf(stderr, "mdb_txn_begin (verify) failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    print_tree(parent_txn, dbi);

    mdb_txn_abort(parent_txn);
    mdb_dbi_close(env, dbi);
    mdb_env_close(env);

    printf("\n=== Demo Complete ===\n");
    printf("\nKey observations:\n");
    printf("- Child transactions see parent's data\n");
    printf("- Parent does NOT see child's uncommitted data\n");
    printf("- When child commits, changes merge to parent\n");
    printf("- When child aborts, changes are discarded\n");

    return 0;
}
