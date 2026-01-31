/**
 * lmdb_debugger.c - Day 14: LMDB 调试辅助工具
 *
 * 这个程序提供了多种诊断功能：
 * - 环境状态检查
 * - 数据库健康检查
 * - 性能测试
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <lmdb.h>

// 打印环境状态
void print_env_status(MDB_env *env) {
    MDB_envinfo info;
    MDB_stat stat;
    MDB_txn *txn;

    printf("\n========== 环境状态 ==========\n");

    // 环境信息
    mdb_env_info(env, &info);
    printf("映射地址:      %p\n", info.me_mapaddr);
    printf("映射大小:      %zu MB\n", info.me_mapsize / (1024*1024));
    printf("最后页号:      %zu\n", (size_t)info.me_last_pgno);
    printf("最大读事务数:  %u\n", info.me_maxreaders);
    printf("当前读事务数:  %u\n", info.me_numreaders);

    // 统计信息
    if (mdb_txn_begin(env, NULL, MDB_RDONLY, &txn) == 0) {
        MDB_dbi dbi;
        if (mdb_dbi_open(txn, NULL, 0, &dbi) == 0) {
            mdb_stat(txn, dbi, &stat);
            printf("\n数据库统计:\n");
            printf("  页面大小:      %u bytes\n", stat.ms_psize);
            printf("  树的深度:      %u\n", stat.ms_depth);
            printf("  分支页数:      %zu\n", (size_t)stat.ms_branch_pages);
            printf("  叶子页数:      %zu\n", (size_t)stat.ms_leaf_pages);
            printf("  溢出页数:      %zu\n", (size_t)stat.ms_overflow_pages);
            printf("  总条目数:      %zu\n", (size_t)stat.ms_entries);

            mdb_dbi_close(env, dbi);
        }
        mdb_txn_abort(txn);
    }

    printf("\n空间使用:\n");
    printf("  已用页面:      %zu\n", (size_t)info.me_last_pgno + 1);
    printf("================================\n\n");
}

// 检查数据库健康状态
int check_database_health(MDB_env *env) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_stat stat;
    int health_score = 100;
    int issues = 0;

    printf("\n========== 健康检查 ==========\n");

    if (mdb_txn_begin(env, NULL, MDB_RDONLY, &txn) != 0) {
        printf("X 无法开始读事务\n");
        return 0;
    }

    if (mdb_dbi_open(txn, NULL, 0, &dbi) != 0) {
        printf("X 无法打开主数据库\n");
        mdb_txn_abort(txn);
        return 0;
    }

    mdb_stat(txn, dbi, &stat);

    // 检查 1: 树深度
    if (stat.ms_depth > 5) {
        printf("! 树深度过深 (%u)，可能影响性能\n", stat.ms_depth);
        health_score -= 10;
        issues++;
    } else {
        printf("√ 树深度正常 (%u)\n", stat.ms_depth);
    }

    // 检查 2: 溢出页比例
    size_t total_pages = (size_t)stat.ms_branch_pages + (size_t)stat.ms_leaf_pages;
    if (total_pages > 0 && (size_t)stat.ms_overflow_pages > total_pages / 10) {
        printf("! 溢出页过多 (%zu / %zu)\n",
               (size_t)stat.ms_overflow_pages, total_pages);
        health_score -= 15;
        issues++;
    } else {
        printf("√ 溢出页数量正常 (%zu)\n", (size_t)stat.ms_overflow_pages);
    }

    // 检查 3: 空数据库
    if (stat.ms_entries == 0) {
        printf("! 数据库为空\n");
        health_score -= 5;
    } else {
        printf("√ 数据库包含 %zu 条记录\n", (size_t)stat.ms_entries);
    }

    mdb_txn_abort(txn);
    mdb_dbi_close(env, dbi);

    // 最终评分
    printf("\n健康评分: %d/100", health_score);
    if (health_score >= 80) {
        printf(" (优秀)\n");
    } else if (health_score >= 60) {
        printf(" (良好)\n");
    } else if (health_score >= 40) {
        printf(" (一般)\n");
    } else {
        printf(" (需要关注)\n");
    }

    if (issues == 0) {
        printf("√ 未发现问题\n");
    } else {
        printf("X 发现 %d 个潜在问题\n", issues);
    }

    printf("================================\n\n");

    return health_score;
}

// 性能测试
void performance_test(MDB_env *env, int num_ops) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;
    struct timespec start, end;
    char key_buf[32], data_buf[32];

    printf("\n========== 性能测试 ==========\n");
    printf("操作数量: %d\n\n", num_ops);

    // 写入测试
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < num_ops; i++) {
        mdb_txn_begin(env, NULL, 0, &txn);
        mdb_dbi_open(txn, NULL, 0, &dbi);

        snprintf(key_buf, sizeof(key_buf), "key-%d", i);
        snprintf(data_buf, sizeof(data_buf), "value-%d", i);

        key.mv_data = key_buf;
        key.mv_size = strlen(key_buf);
        data.mv_data = data_buf;
        data.mv_size = strlen(data_buf);

        mdb_put(txn, dbi, &key, &data, 0);
        mdb_txn_commit(txn);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    double write_time = (end.tv_sec - start.tv_sec) +
                        (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("写入性能:\n");
    printf("  总耗时: %.3f 秒\n", write_time);
    if (write_time > 0) {
        printf("  吞吐量: %.0f ops/s\n", num_ops / write_time);
    }

    // 读取测试
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < num_ops; i++) {
        mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
        mdb_dbi_open(txn, NULL, 0, &dbi);

        snprintf(key_buf, sizeof(key_buf), "key-%d", i);
        key.mv_data = key_buf;
        key.mv_size = strlen(key_buf);

        mdb_get(txn, dbi, &key, &data);

        mdb_txn_abort(txn);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    double read_time = (end.tv_sec - start.tv_sec) +
                       (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("\n读取性能:\n");
    printf("  总耗时: %.3f 秒\n", read_time);
    if (read_time > 0) {
        printf("  吞吐量: %.0f ops/s\n", num_ops / read_time);
    }

    printf("================================\n\n");
}

int main(int argc, char **argv) {
    MDB_env *env;
    char *db_path = "./testdb";
    int rc;

    if (argc > 1) {
        db_path = argv[1];
    }

    printf("=== LMDB 调试工具 ===\n");
    printf("数据库路径: %s\n", db_path);

    // 打开环境
    rc = mdb_env_create(&env);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_create failed: %s\n", mdb_strerror(rc));
        return 1;
    }

    rc = mdb_env_open(env, db_path, 0, 0664);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_open failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    // 运行诊断
    print_env_status(env);
    check_database_health(env);
    performance_test(env, 1000);

    mdb_env_close(env);

    return 0;
}
