/**
 * page_allocator.c - LMDB 页面分配器演示
 *
 * 这个程序深入讲解 LMDB 的页面分配和空闲列表管理。
 *
 * 学习目标：
 * - 理解页面分配机制
 * - 理解空闲列表管理
 * - 理解页面生命周期
 * - 理解 MVCC 如何影响页面重用
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lmdb.h>

#define COLOR_RESET   "\033[0m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_RED     "\033[31m"

/**
 * 演示页面生命周期
 */
void demonstrate_page_lifecycle() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   页面生命周期                                              ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("页面从分配到释放的完整流程：\n");
    printf("\n");

    printf("  1. " COLOR_GREEN "分配" COLOR_RESET " (mdb_page_alloc)\n");
    printf("     │\n");
    printf("     ├─► 检查空闲列表\n");
    printf("     │   ├─ 有可用页？ → 重用\n");
    printf("     │   └─ 无可用页？ → 扩展文件\n");
    printf("     │\n");
    printf("     ├─► 标记为脏页\n");
    printf("     │\n");
    printf("     └─► 返回页面指针\n");
    printf("\n");

    printf("  2. " COLOR_YELLOW "使用" COLOR_RESET " (正常操作)\n");
    printf("     │\n");
    printf("     ├─► 存储数据\n");
    printf("     ├─► 可能被多个读者引用\n");
    printf("     └─► 保持当前版本\n");
    printf("\n");

    printf("  3. " COLOR_CYAN "修改" COLOR_RESET " (写时复制)\n");
    printf("     │\n");
    printf("     ├─► 分配新页\n");
    printf("     ├─► 复制旧页内容\n");
    printf("     ├─► 应用修改\n");
    printf("     └─► 旧页等待所有读者完成\n");
    printf("\n");

    printf("  4. " COLOR_RED "释放" COLOR_RESET " (添加到空闲列表)\n");
    printf("     │\n");
    printf("     ├─► 检查是否还有读者\n");
    printf("     ├─► 所有旧读者完成？\n");
    printf("     ├─► 是 → 添加到空闲列表\n");
    printf("     └─► 否 → 延迟释放\n");
    printf("\n");

    printf("  5. " COLOR_GREEN "重用" COLOR_RESET " (再次分配)\n");
    printf("     └─► 回到步骤 1\n");
}

/**
 * 演示空闲列表结构
 */
void demonstrate_freelist_structure() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   空闲列表结构                                            ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("空闲列表存储在元数据库 (MDB_db with ID 1)\n");
    printf("\n");

    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│  键: pgno_t (页面号)                                    │\n");
    printf("│  值: txnid (释放该页的事务 ID)                          │\n");
    printf("└─────────────────────────────────────────────────────────┘\n");
    printf("\n");

    printf("示例空闲列表：\n");
    printf("┌──────────────┬──────────┬────────────────────────────┐\n");
    printf("│ 页面号        │ 释放事务 │ 状态                        │\n");
    printf("├──────────────┼──────────┼────────────────────────────┤\n");
    printf("│ 50           │ 100      │ " COLOR_GREEN "可重用" COLOR_RESET " (读者都 >100)   │\n");
    printf("│ 75           │ 105      │ " COLOR_YELLOW "保留中" COLOR_RESET " (读者 102 还活跃)  │\n");
    printf("│ 100          │ 110      │ " COLOR_YELLOW "保留中" COLOR_RESET " (读者 108 还活跃)  │\n");
    printf("│ 120          │ 95       │ " COLOR_GREEN "可重用" COLOR_RESET " (读者都 >95)     │\n");
    printf("└──────────────┴──────────┴────────────────────────────┘\n");
    printf("\n");

    printf(COLOR_YELLOW "重用判断：" COLOR_RESET "\n");
    printf("  当前事务: 115\n");
    printf("  最老活跃读者: 102\n");
    printf("\n");
    printf("  页面 50 (txn:100) → 100 < 102 ✓ " COLOR_GREEN "可重用" COLOR_RESET "\n");
    printf("  页面 75 (txn:105) → 105 >= 102 ✗ " COLOR_YELLOW "不可重用" COLOR_RESET "\n");
    printf("  页面 100 (txn:110) → 110 >= 102 ✗ " COLOR_YELLOW "不可重用" COLOR_RESET "\n");
}

/**
 * 演示页面分配算法
 */
void demonstrate_allocation_algorithm() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   页面分配算法                                            ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("mdb_page_alloc() 伪代码：\n");
    printf("\n");

    printf("  " COLOR_CYAN "mdb_page_alloc(txn, num):" COLOR_RESET "\n");
    printf("  │\n");
    printf("  ├─► 遍历空闲列表\n");
    printf("  │   │\n");
    printf("  │   ├─ 对于每个空闲页:\n");
    printf("  │   │   │\n");
    printf("  │   │   ├─ if (页面的 txn_id < 最老读者的 txn_id):\n");
    printf("  │   │   │      │\n");
    printf("  │   │   │      ├─ 该页可重用!\n");
    printf("  │   │   │      ├─ 从空闲列表删除\n");
    printf("  │   │   │      ├─ 初始化页面\n");
    printf("  │   │   │      └─ return 页面\n");
    printf("  │   │   │\n");
    printf("  │   │   └─ else:\n");
    printf("  │   │       └─ 继续查找\n");
    printf("  │   │\n");
    printf("  │   └─ 找不到可用页？\n");
    printf("  │       │\n");
    printf("  │       └─► 扩展数据库文件\n");
    printf("  │           │\n");
    printf("  │           ├─ 计算新文件大小\n");
    printf("  │           ├─ 调用 ftruncate()\n");
    printf("  │           ├─ 重新内存映射 (mremap/mmap)\n");
    printf("  │           └─ 返回新页面\n");
    printf("  │\n");
    printf("  └─► return NULL (失败)\n");
}

/**
 * 演示事务与页面的关系
 */
void demonstrate_transaction_pages() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   事务与页面的关系                                        ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("写事务跟踪的页面信息：\n");
    printf("\n");

    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│  MDB_txn 中的页面跟踪:                                 │\n");
    printf("│                                                         │\n");
    printf("│  mt_next_pgno: 下一个可分配的页面号                      │\n");
    printf("│  mt_rpages:    此事务释放的页面链表                      │\n");
    printf("│  mt_u:         读者槽位指针（只读事务）                  │\n");
    printf("└─────────────────────────────────────────────────────────┘\n");
    printf("\n");

    printf("示例场景：\n");
    printf("\n");

    printf("事务 100 开始：\n");
    printf("  mt_next_pgno = 150 (下一个新页将是 150)\n");
    printf("  mt_rpages = NULL (尚未释放任何页)\n");
    printf("\n");

    printf("事务 100 修改页面 50：\n");
    printf("  " COLOR_CYAN "分配新页 150" COLOR_RESET "\n");
    printf("  " COLOR_GREEN "复制页 50 到 150" COLOR_RESET "\n");
    printf("  " COLOR_YELLOW "在 150 上修改" COLOR_RESET "\n");
    printf("  页面 50 等待读者\n");
    printf("\n");

    printf("事务 100 提交：\n");
    printf("  " COLOR_RED "页面 50 添加到空闲列表" COLOR_RESET " (txn_id: 100)\n");
    printf("  mt_next_pgno = 151\n");
    printf("\n");

    printf("事务 110 开始：\n");
    printf("  需要新页？\n");
    printf("  检查空闲列表: [(50, txn:100), ...]\n");
    printf("  最老读者: 105\n");
    printf("  100 < 105? 是! → 页面 50 " COLOR_GREEN "可重用!" COLOR_RESET "\n");
}

/**
 * 演示页面重用的时机
 */
void demonstrate_reuse_timing() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   页面重用时机                                            ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("时间线示例：\n");
    printf("\n");

    printf("  T0: 事务 100 提交，页面 50 过时\n");
    printf("      → 添加到空闲列表: (50, txn:100)\n");
    printf("\n");

    printf("  T1: 读者 A (txn_id:102) 仍在使用页面 50\n");
    printf("      事务 105 尝试分配页面\n");
    printf("      → 检查页面 50: txn:100 < 102? 否!\n");
    printf("      → " COLOR_YELLOW "不能重用" COLOR_RESET "，读者 A 还需要它\n");
    printf("\n");

    printf("  T2: 读者 A 完成 (txn_id:102 结束)\n");
    printf("      事务 110 尝试分配页面\n");
    printf("      → 检查页面 50: txn:100 < 110? 是!\n");
    printf("      → " COLOR_GREEN "可以重用!" COLOR_RESET " 读者 A 已完成\n");
    printf("\n");

    printf(COLOR_YELLOW "关键规则：" COLOR_RESET "\n");
    printf("  页面 P (txn:T) 可被重用，当且仅当:\n");
    printf("    T < min(所有活跃读者的 txn_id)\n");
    printf("\n");

    printf("  这确保没有读者会看到过时的数据！\n");
}

/**
 * 实际演示
 */
void demonstrate_real_allocation() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   实际页面分配演示                                        ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET);

    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;

    printf("\n创建数据库并插入数据...\n");

    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024 * 100);
    mdb_env_open(env, "./page_alloc_demo", 0, 0664);

    /* 第一次插入 */
    mdb_txn_begin(env, NULL, 0, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);

    for (int i = 0; i < 100; i++) {
        char key[32], value[32];
        snprintf(key, sizeof(key), "key%05d", i);
        snprintf(value, sizeof(value), "value%05d_data_here", i);

        MDB_val k = { .mv_size = strlen(key), .mv_data = key };
        MDB_val v = { .mv_size = strlen(value), .mv_data = value };
        mdb_put(txn, dbi, &k, &v, 0);
    }

    mdb_txn_commit(txn);

    /* 获取统计 */
    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);

    MDB_stat stat;
    mdb_stat(txn, dbi, &stat);

    printf("\n第一次插入后的统计：\n");
    printf("  使用的页面数: %zu\n", stat.ms_branch_pages + stat.ms_leaf_pages);
    printf("  树深度: %u\n", stat.ms_depth);

    mdb_txn_abort(txn);

    /* 更新数据（触发 COW）*/
    printf("\n更新数据（触发写时复制）...\n");

    mdb_txn_begin(env, NULL, 0, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);

    for (int i = 0; i < 50; i++) {
        char key[32], value[32];
        snprintf(key, sizeof(key), "key%05d", i);
        snprintf(value, sizeof(value), "updated_value%05d", i);

        MDB_val k = { .mv_size = strlen(key), .mv_data = key };
        MDB_val v = { .mv_size = strlen(value), .mv_data = value };
        mdb_put(txn, dbi, &k, &v, 0);
    }

    mdb_txn_commit(txn);

    /* 再次获取统计 */
    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);
    mdb_stat(txn, dbi, &stat);

    printf("\n更新后的统计：\n");
    printf("  使用的页面数: %zu\n", stat.ms_branch_pages + stat.ms_leaf_pages);
    printf("  树深度: %u\n", stat.ms_depth);

    printf("\n" COLOR_YELLOW "注意：页面数增加了！" COLOR_RESET);
    printf("\n  原因：更新使用了写时复制，分配了新页面");
    printf("\n  旧页面等待所有读者完成后才能重用");

    mdb_txn_abort(txn);
    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}

/**
 * 显示页面分配性能考虑
 */
void show_performance_considerations() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   页面分配性能考虑                                        ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf(COLOR_YELLOW "1. 预分配 vs 按需分配：" COLOR_RESET "\n");
    printf("  • LMDB 按需分配（只在需要时扩展文件）\n");
    printf("  • 优点：不浪费磁盘空间\n");
    printf("  • 缺点：首次写入可能稍慢\n");
    printf("\n");

    printf(COLOR_YELLOW "2. 空闲列表查找：" COLOR_RESET "\n");
    printf("  • 遍历空闲列表找可用页\n");
    printf("  • 最坏情况 O(n)，n 是空闲页数\n");
    printf("  • 优化：维护多个列表（按事务 ID）\n");
    printf("\n");

    printf(COLOR_YELLOW "3. 文件扩展开销：" COLOR_RESET "\n");
    printf("  • ftruncate() 系统调用\n");
    printf("  • 可能重新 mmap()\n");
    printf("  • 建议：设置合理的 mapsize\n");
    printf("\n");

    printf(COLOR_YELLOW "4. 批量操作优化：" COLOR_RESET "\n");
    printf("  • 单个事务中多次操作更高效\n");
    printf("  • 重用事务内的页面\n");
    printf("  • 减少提交次数\n");
    printf("\n");

    printf(COLOR_YELLOW "5. 空间回收延迟：" COLOR_RESET "\n");
    printf("  • 页面不会立即归还给文件系统\n");
    printf("  • 空间在 LMDB 内重用\n");
    printf("  • 文件大小通常不缩小\n");
}

int main() {
    printf(COLOR_CYAN "\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     LMDB 页面分配器深度解析                               ║\n");
    printf("║     Page Allocator Deep Dive                             ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf(COLOR_RESET);

    demonstrate_page_lifecycle();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 继续..." COLOR_RESET);
    getchar();

    demonstrate_freelist_structure();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 继续..." COLOR_RESET);
    getchar();

    demonstrate_allocation_algorithm();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 继续..." COLOR_RESET);
    getchar();

    demonstrate_transaction_pages();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 继续..." COLOR_RESET);
    getchar();

    demonstrate_reuse_timing();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 继续实际演示..." COLOR_RESET);
    getchar();

    demonstrate_real_allocation();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 查看性能考虑..." COLOR_RESET);
    getchar();

    show_performance_considerations();

    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   总结                                                    ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf(COLOR_YELLOW "页面分配核心概念：" COLOR_RESET "\n");
    printf("  1. 空闲列表管理可重用页面\n");
    printf("  2. MVCC 决定页面何时可重用\n");
    printf("  3. 写时复制创建新页面\n");
    printf("  4. 旧页等待所有读者完成后释放\n");
    printf("  5. 空间在内部重用，不归还文件系统\n");

    printf("\n" COLOR_YELLOW "关键函数（源码位置）：" COLOR_RESET "\n");
    printf("  mdb_page_alloc()    - 分配页面 (约行 2800)\n");
    printf("  mdb_page_touch()    - COW 标记 (约行 3200)\n");
    printf("  mdb_page_new()      - 初始化新页 (约行 2900)\n");
    printf("  mdb_page_dispose()  - 释放页面 (约行 3100)\n");

    printf(COLOR_GREEN "\n✓ page_allocator 完成!\n" COLOR_RESET);
    printf("  数据库: ./page_alloc_demo/\n");

    return 0;
}
