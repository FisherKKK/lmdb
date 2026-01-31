/**
 * visualize_db.c - LMDB 数据库可视化工具
 *
 * 这个程序以图形化方式展示 LMDB 数据库的内部结构：
 * - B+树结构可视化
 * - 页面布局展示
 * - 节点分布统计
 * - 空间使用分析
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lmdb.h>

// 颜色输出
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_WHITE   "\033[37m"

// 打印树结构的辅助信息
typedef struct {
    int depth;
    int position;
    int is_last;
    int prefix_depth;
} tree_context_t;

// 递归打印树结构（简化版，基于统计信息）
void print_tree_structure(MDB_stat *stat) {
    printf("\n" COLOR_CYAN "========== B+ Tree Structure ==========" COLOR_RESET "\n\n");

    printf("Root Node\n");
    printf("│\n");

    if (stat->ms_depth > 1) {
        printf("├─ Branch Level %d\n", stat->ms_depth - 1);
        printf("│  │\n");

        for (int i = 1; i < stat->ms_depth - 1; i++) {
            printf("│  ├─ Branch Level %d\n", stat->ms_depth - 1 - i);
            printf("│  │  │\n");
        }

        printf("│  └─ " COLOR_GREEN "Leaf Nodes" COLOR_RESET "\n");
        printf("│     ├─ [Entry 1] [Entry 2] ... [Entry N]\n");
        printf("│     └─ Total: " COLOR_YELLOW "%zu" COLOR_RESET " entries\n",
               (size_t)stat->ms_entries);
    } else {
        printf("└─ " COLOR_GREEN "Leaf Nodes (direct)" COLOR_RESET "\n");
        printf("   └─ [Entry 1] [Entry 2] ... [Entry N]\n");
    }

    printf("\n");
}

// 打印页面布局
void print_page_layout(MDB_stat *stat) {
    printf(COLOR_CYAN "========== Page Layout Analysis ==========" COLOR_RESET "\n\n");

    size_t total_pages = (size_t)stat->ms_branch_pages +
                        (size_t)stat->ms_leaf_pages +
                        (size_t)stat->ms_overflow_pages;

    printf("Total Pages: " COLOR_YELLOW "%zu" COLOR_RESET "\n", total_pages);
    printf("\n");

    // 使用简单的条形图表示
    int bar_width = 50;
    size_t max_count = total_pages;

    printf("Branch Pages: " COLOR_BLUE "%5zu" COLOR_RESET " ",
           (size_t)stat->ms_branch_pages);
    if (max_count > 0) {
        int len = (int)((size_t)stat->ms_branch_pages * bar_width / max_count);
        for (int i = 0; i < len; i++) printf(COLOR_BLUE "█" COLOR_RESET);
    }
    printf(" " COLOR_YELLOW "%.1f%%" COLOR_RESET "\n",
           total_pages > 0 ? 100.0 * stat->ms_branch_pages / total_pages : 0);

    printf("Leaf Pages:   " COLOR_GREEN "%5zu" COLOR_RESET " ",
           (size_t)stat->ms_leaf_pages);
    if (max_count > 0) {
        int len = (int)((size_t)stat->ms_leaf_pages * bar_width / max_count);
        for (int i = 0; i < len; i++) printf(COLOR_GREEN "█" COLOR_RESET);
    }
    printf(" " COLOR_YELLOW "%.1f%%" COLOR_RESET "\n",
           total_pages > 0 ? 100.0 * stat->ms_leaf_pages / total_pages : 0);

    printf("Overflow Pg:  " COLOR_RED "%5zu" COLOR_RESET " ",
           (size_t)stat->ms_overflow_pages);
    if (max_count > 0) {
        int len = (int)((size_t)stat->ms_overflow_pages * bar_width / max_count);
        for (int i = 0; i < len; i++) printf(COLOR_RED "█" COLOR_RESET);
    }
    printf(" " COLOR_YELLOW "%.1f%%" COLOR_RESET "\n",
           total_pages > 0 ? 100.0 * stat->ms_overflow_pages / total_pages : 0);

    printf("\n");
}

// 打印页面内存布局
void print_page_memory_layout(unsigned int psize) {
    printf(COLOR_CYAN "========== Page Memory Layout ==========" COLOR_RESET "\n\n");
    printf("Page Size: " COLOR_YELLOW "%u" COLOR_RESET " bytes\n\n", psize);

    // MDB_page structure size (approximate from public API)
    size_t header_size = 16;  // Approximate header size

    printf("┌" COLOR_CYAN "────────────────────────────────────────┐" COLOR_RESET "\n");
    printf("│" COLOR_CYAN " Page Header (%-3zu bytes)"               COLOR_RESET "│\n",
           header_size);
    printf("├" COLOR_CYAN "────────────────────────────────────────┤" COLOR_RESET "\n");

    int remaining = psize - header_size;
    int ptrs_area = remaining / 4;
    int data_area = remaining - ptrs_area;

    printf("│ " COLOR_BLUE "Ptrs Area (%-4d bytes)" COLOR_RESET "     │\n", ptrs_area);
    printf("│  ┌────────────────────────────────────┐   │\n");
    printf("│  │ Ptr 0 │ Ptr 1 │ ... │ Ptr N │     │   │\n");
    printf("│  └────────────────────────────────────┘   │\n");
    printf("│ " COLOR_GREEN "Data Area (%-4d bytes)" COLOR_RESET "     │\n", data_area);
    printf("│  ┌────────────────────────────────────┐   │\n");
    printf("│  │ KeyN │ ... │ Key1 │ Key0 │ Data    │   │\n");
    printf("│  │ grows up ↑ │ grows down ↓ │        │   │\n");
    printf("│  └────────────────────────────────────┘   │\n");
    printf("└" COLOR_CYAN "────────────────────────────────────────┘" COLOR_RESET "\n\n");

    printf("Legend:\n");
    printf("  " COLOR_BLUE "Ptrs Area" COLOR_RESET "  - Node pointers grow upward\n");
    printf("  " COLOR_GREEN "Data Area" COLOR_RESET "  - Keys and data grow downward\n");
    printf("  Free space is between the two areas\n\n");
}

// 打印键值分布分析
void analyze_key_distribution(MDB_env *env) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_cursor *cursor;
    MDB_val key, data;
    int rc;

    printf(COLOR_CYAN "========== Key Distribution Analysis ==========" COLOR_RESET "\n\n");

    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) {
        fprintf(stderr, "Cannot begin transaction\n");
        return;
    }

    rc = mdb_dbi_open(txn, NULL, 0, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return;
    }

    rc = mdb_cursor_open(txn, dbi, &cursor);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return;
    }

    // 统计键长度分布
    int key_len_bins[10] = {0};
    int total_keys = 0;
    size_t total_key_size = 0;
    size_t total_data_size = 0;

    rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    while (rc == 0) {
        total_keys++;
        total_key_size += key.mv_size;
        total_data_size += data.mv_size;

        int bin = (int)(key.mv_size / 10);
        if (bin >= 10) bin = 9;
        key_len_bins[bin]++;

        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);

    printf("Total Keys: " COLOR_YELLOW "%d" COLOR_RESET "\n", total_keys);
    printf("Avg Key Size: " COLOR_YELLOW "%.1f" COLOR_RESET " bytes\n",
           total_keys > 0 ? (double)total_key_size / total_keys : 0);
    printf("Avg Data Size: " COLOR_YELLOW "%.1f" COLOR_RESET " bytes\n",
           total_keys > 0 ? (double)total_data_size / total_keys : 0);

    printf("\nKey Length Distribution:\n");
    for (int i = 0; i < 10; i++) {
        if (key_len_bins[i] > 0) {
            int bar_len = (int)(key_len_bins[i] * 40 / total_keys);
            printf("  [%2d-%2d]: ", i * 10, (i + 1) * 10 - 1);
            for (int j = 0; j < bar_len; j++) printf(COLOR_GREEN "█" COLOR_RESET);
            printf(" " COLOR_YELLOW "%d" COLOR_RESET "\n", key_len_bins[i]);
        }
    }
    printf("\n");
}

// 打印空间效率分析
void analyze_space_efficiency(MDB_env *env, MDB_stat *stat) {
    MDB_envinfo info;
    size_t total_map_size;
    size_t used_space;
    size_t free_space;
    double efficiency;

    mdb_env_info(env, &info);

    printf(COLOR_CYAN "========== Space Efficiency ==========" COLOR_RESET "\n\n");

    total_map_size = info.me_mapsize;
    used_space = (stat->ms_branch_pages + stat->ms_leaf_pages +
                 stat->ms_overflow_pages + 2) * stat->ms_psize;
    free_space = total_map_size - used_space;

    printf("Total Map Size:    " COLOR_YELLOW "%.2f MB" COLOR_RESET "\n",
           total_map_size / (1024.0 * 1024));
    printf("Used Space:        " COLOR_GREEN "%.2f MB" COLOR_RESET "\n",
           used_space / (1024.0 * 1024));
    printf("Free Space:        " COLOR_BLUE "%.2f MB" COLOR_RESET "\n",
           free_space / (1024.0 * 1024));

    efficiency = total_map_size > 0 ? 100.0 * used_space / total_map_size : 0;
    printf("\nSpace Efficiency:  " COLOR_YELLOW "%.2f%%" COLOR_RESET "\n", efficiency);

    printf("\nSpace Utilization:\n");
    int bar_len = (int)(efficiency / 2);
    printf("  [");
    for (int i = 0; i < 50; i++) {
        if (i < bar_len)
            printf(COLOR_GREEN "█" COLOR_RESET);
        else
            printf("░");
    }
    printf("] %.1f%%\n\n", efficiency);
}

// 打印健康评分
void print_health_score(MDB_stat *stat) {
    int score = 100;
    int issues = 0;

    printf(COLOR_CYAN "========== Health Score ==========" COLOR_RESET "\n\n");

    // 检查树深度
    if (stat->ms_depth > 5) {
        printf(COLOR_RED "  ⚠ Tree depth is high (%d)" COLOR_RESET "\n", stat->ms_depth);
        score -= 10;
        issues++;
    } else {
        printf(COLOR_GREEN "  ✓ Tree depth is good (%d)" COLOR_RESET "\n", stat->ms_depth);
    }

    // 检查溢出页比例
    size_t total = stat->ms_branch_pages + stat->ms_leaf_pages;
    if (total > 0 && stat->ms_overflow_pages > total / 10) {
        printf(COLOR_RED "  ⚠ Too many overflow pages (%zu / %zu)" COLOR_RESET "\n",
               (size_t)stat->ms_overflow_pages, total);
        score -= 15;
        issues++;
    } else {
        printf(COLOR_GREEN "  ✓ Overflow pages ratio is good" COLOR_RESET "\n");
    }

    // 检查条目数
    if (stat->ms_entries == 0) {
        printf(COLOR_YELLOW "  ⚠ Database is empty" COLOR_RESET "\n");
        score -= 5;
        issues++;
    } else {
        printf(COLOR_GREEN "  ✓ Database contains %zu entries" COLOR_RESET "\n",
               (size_t)stat->ms_entries);
    }

    printf("\n");
    if (score >= 80) {
        printf(COLOR_GREEN "Health Score: %d/100 (Excellent)" COLOR_RESET "\n", score);
    } else if (score >= 60) {
        printf(COLOR_YELLOW "Health Score: %d/100 (Good)" COLOR_RESET "\n", score);
    } else if (score >= 40) {
        printf(COLOR_RED "Health Score: %d/100 (Fair)" COLOR_RESET "\n", score);
    } else {
        printf(COLOR_RED "Health Score: %d/100 (Poor)" COLOR_RESET "\n", score);
    }

    if (issues == 0) {
        printf(COLOR_GREEN "No issues found!" COLOR_RESET "\n");
    } else {
        printf(COLOR_YELLOW "%d issue(s) found" COLOR_RESET "\n", issues);
    }
    printf("\n");
}

int main(int argc, char **argv) {
    MDB_env *env;
    MDB_stat stat;
    MDB_envinfo info;
    char *db_path = "./testdb";
    int rc;

    if (argc > 1) {
        db_path = argv[1];
    }

    printf(COLOR_CYAN "========================================" COLOR_RESET "\n");
    printf(COLOR_CYAN "    LMDB Database Visualizer v1.0    " COLOR_RESET "\n");
    printf(COLOR_CYAN "========================================" COLOR_RESET "\n");
    printf("\nDatabase: %s\n\n", db_path);

    rc = mdb_env_create(&env);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_create failed: %s\n", mdb_strerror(rc));
        return 1;
    }

    rc = mdb_env_open(env, db_path, MDB_RDONLY, 0664);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_open failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    // 获取统计信息
    mdb_env_info(env, &info);
    MDB_txn *txn;
    if (mdb_txn_begin(env, NULL, MDB_RDONLY, &txn) == 0) {
        MDB_dbi dbi;
        if (mdb_dbi_open(txn, NULL, 0, &dbi) == 0) {
            mdb_stat(txn, dbi, &stat);

            // 打印基本信息（在事务关闭前）
            print_tree_structure(&stat);
            print_page_layout(&stat);
            print_page_memory_layout(stat.ms_psize);
            print_health_score(&stat);

            mdb_dbi_close(env, dbi);
        }
        mdb_txn_abort(txn);
    }

    // 其他分析需要独立事务
    analyze_key_distribution(env);
    analyze_space_efficiency(env, &stat);

    mdb_env_close(env);

    printf(COLOR_CYAN "========================================" COLOR_RESET "\n");
    printf(COLOR_GREEN "        Visualization Complete!     " COLOR_RESET "\n");
    printf(COLOR_CYAN "========================================" COLOR_RESET "\n");

    return 0;
}
