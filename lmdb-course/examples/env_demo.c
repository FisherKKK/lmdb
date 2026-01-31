/**
 * env_demo.c - Day 3: 环境管理演示
 *
 * 演示 LMDB 环境的创建、配置和管理
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lmdb.h>

void print_env_stats(MDB_env *env) {
    MDB_envinfo info;
    MDB_stat stat;
    MDB_txn *txn;

    printf("=== Environment Statistics ===\n");

    // 环境信息
    mdb_env_info(env, &info);
    printf("Map address:   %p\n", info.me_mapaddr);
    printf("Map size:      %.2f MB\n", (double)info.me_mapsize / (1024 * 1024));
    printf("Last page no:  %zu\n", (size_t)info.me_last_pgno);
    printf("Max readers:   %u\n", info.me_maxreaders);
    printf("Num readers:   %u\n", info.me_numreaders);

    // 获取数据库统计
    if (mdb_txn_begin(env, NULL, MDB_RDONLY, &txn) == 0) {
        if (mdb_stat(txn, 1, &stat) == 0) {
            printf("\nMain DB Stats:\n");
            printf("  Page size:      %u\n", stat.ms_psize);
            printf("  Tree depth:     %u\n", stat.ms_depth);
            printf("  Branch pages:   %zu\n", (size_t)stat.ms_branch_pages);
            printf("  Leaf pages:     %zu\n", (size_t)stat.ms_leaf_pages);
            printf("  Overflow pages: %zu\n", (size_t)stat.ms_overflow_pages);
            printf("  Entries:        %zu\n", (size_t)stat.ms_entries);
        }
        mdb_txn_abort(txn);
    }
}

void print_readers(MDB_env *env) {
    // Check if we can get reader info using mdb_reader_check
    int dead = 0;
    int rc = mdb_reader_check(env, &dead);
    if (rc == 0) {
        printf("\n=== Reader Table ===\n");
        printf("mdb_reader_check: %d stale reader(s) cleaned up\n", dead);
    } else {
        printf("\n=== Reader Table ===\n");
        printf("Unable to check readers (may be MDB_NOLOCK mode)\n");
    }
}

int main() {
    MDB_env *env;
    int rc;

    printf("=== LMDB Environment Management Demo ===\n\n");

    // 1. 创建环境
    printf("Step 1: Creating environment...\n");
    rc = mdb_env_create(&env);
    if (rc) {
        fprintf(stderr, "mdb_env_create failed: %s\n", mdb_strerror(rc));
        return 1;
    }
    printf("  Environment created successfully\n");

    // 2. 配置环境
    printf("\nStep 2: Configuring environment...\n");
    mdb_env_set_mapsize(env, 1024 * 1024 * 50);  // 50MB
    mdb_env_set_maxdbs(env, 8);
    mdb_env_set_maxreaders(env, 64);
    printf("  Map size: 50MB\n");
    printf("  Max databases: 8\n");
    printf("  Max readers: 64\n");

    // 3. 打开环境
    printf("\nStep 3: Opening environment...\n");
    rc = mdb_env_open(env, "./testdb", MDB_NOSYNC, 0664);
    if (rc) {
        fprintf(stderr, "mdb_env_open failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }
    printf("  Environment opened at './testdb'\n");

    // 4. 打印环境信息
    printf("\n");
    print_env_stats(env);

    // 5. 测试多个数据库
    printf("\n=== Testing Multiple Databases ===\n");
    MDB_txn *txn;
    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc) {
        fprintf(stderr, "mdb_txn_begin failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    // 打开几个命名数据库
    MDB_dbi users_db, posts_db, comments_db;
    rc = mdb_dbi_open(txn, "users", MDB_CREATE, &users_db);
    if (rc) {
        fprintf(stderr, "mdb_dbi_open (users) failed: %s\n", mdb_strerror(rc));
    }
    rc = mdb_dbi_open(txn, "posts", MDB_CREATE, &posts_db);
    if (rc) {
        fprintf(stderr, "mdb_dbi_open (posts) failed: %s\n", mdb_strerror(rc));
    }
    rc = mdb_dbi_open(txn, "comments", MDB_CREATE, &comments_db);
    if (rc) {
        fprintf(stderr, "mdb_dbi_open (comments) failed: %s\n", mdb_strerror(rc));
    }

    printf("Opened databases:\n");
    printf("  users: dbi=%d\n", users_db);
    printf("  posts: dbi=%d\n", posts_db);
    printf("  comments: dbi=%d\n", comments_db);

    mdb_txn_commit(txn);

    // 6. 查看读者表状态
    print_readers(env);

    // 7. 关闭环境
    printf("\n=== Closing environment ===\n");
    mdb_env_close(env);

    printf("\nDone!\n");
    return 0;
}
