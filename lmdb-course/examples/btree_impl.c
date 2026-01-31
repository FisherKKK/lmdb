/**
 * btree_impl.c - LMDB B+树实现详细演示
 *
 * 这个程序深入讲解 LMDB 中 B+ 树的实现细节。
 *
 * 学习目标：
 * - 理解 B+ 树节点分裂过程
 * - 理解 B+ 树节点合并过程
 * - 理解搜索路径和算法
 * - 理解页面管理和重用
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
 * 演示 B+ 树搜索算法
 */
void demonstrate_search_algorithm() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   B+ 树搜索算法                                            ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("搜索 'cherry' 的过程（树深度 3）：\n");
    printf("\n");

    printf("Step 1: 从根页开始\n");
    printf("  " COLOR_CYAN "根页 (Page 2) - 分支页" COLOR_RESET "\n");
    printf("  Keys: [cherry, pear, zebra]\n");
    printf("\n");

    printf("Step 2: 二分查找确定路径\n");
    printf("  比较: 'cherry' vs 'cherry' → 相等!\n");
    printf("  走中间分支 → Page 5\n");
    printf("\n");

    printf("Step 3: 继续搜索中间子树\n");
    printf("  " COLOR_CYAN "Page 5 - 分支页" COLOR_RESET "\n");
    printf("  Keys: [coconut, grape, lemon]\n");
    printf("  比较: 'cherry' vs 'coconut' → 小于\n");
    printf("  走左分支 → Page 10\n");
    printf("\n");

    printf("Step 4: 到达叶页\n");
    printf("  " COLOR_GREEN "Page 10 - 叶页" COLOR_RESET "\n");
    printf("  Keys: [celery, cherry, chestnut]\n");
    printf("  二分查找: 找到 'cherry'!\n");
    printf("  返回对应的值\n");
    printf("\n");

    printf(COLOR_YELLOW "关键代码（mdb.c 简化版）：" COLOR_RESET "\n");
    printf("  │\n");
    printf("  ├─► mdb_cursor_set()\n");
    printf("  │    │\n");
    printf("  │    └─► mdb_page_search()\n");
    printf("  │          │\n");
    printf("  │          ├─► 从根页开始\n");
    printf("  │          │\n");
    printf("  │          ├─► while (是分支页) {\n");
    printf("  │          │      node = 二分查找(页面, 键);\n");
    printf("  │          │      页面 = node->子页;\n");
    printf("  │          │    }\n");
    printf("  │          │\n");
    printf("  │          └─► return 叶页中的节点\n");
}

/**
 * 演示节点分裂过程
 */
void demonstrate_split_process() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   B+ 树节点分裂过程                                        ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("场景：叶页已满，需要插入新键\n");
    printf("\n");

    printf("初始状态（叶页已满）：\n");
    printf("  " COLOR_CYAN "Page 10 - 叶页 (5个键，满)" COLOR_RESET "\n");
    printf("  [apple, banana, cherry, date, elderberry]\n");
    printf("\n");

    printf("Step 1: 插入 'fig' (按字母顺序排在 date 后)\n");
    printf("  但页面已满（假设最多 5 个键）\n");
    printf("\n");

    printf("Step 2: 分裂页面\n");
    printf("  " COLOR_YELLOW "分配新页 Page 20" COLOR_RESET "\n");
    printf("  将前半部分复制到 Page 10:\n");
    printf("    [apple, banana]\n");
    printf("  将后半部分复制到 Page 20:\n");
    printf("    [date, elderberry]\n");
    printf("  中间键 'cherry' 提升到父页\n");
    printf("\n");

    printf("Step 3: 更新父页\n");
    printf("  " COLOR_CYAN "父页 (Page 2) - 分支页" COLOR_RESET "\n");
    printf("  添加新指针: [cherry] → Page 20\n");
    printf("\n");

    printf("分裂后的结构：\n");
    printf("  " COLOR_CYAN "Page 2 (分支)" COLOR_RESET "\n");
    printf("    [cherry]\n");
    printf("      │\n");
    printf("      ├─── " COLOR_GREEN "Page 10 (叶)" COLOR_RESET ": [apple, banana]\n");
    printf("      │\n");
    printf("      └─── " COLOR_GREEN "Page 20 (叶)" COLOR_RESET ": [date, elderberry]\n");
    printf("\n");

    printf("Step 4: 插入新键 'fig'\n");
    printf("  搜索: fig > cherry → 走右分支 → Page 20\n");
    printf("  插入: Page 20 现在有空间\n");
    printf("  " COLOR_GREEN "Page 20 (叶)" COLOR_RESET ": [date, elderberry, fig]\n");

    printf("\n" COLOR_YELLOW "分裂策略：" COLOR_RESET "\n");
    printf("  • 叶页分裂：中间键提升到父页\n");
    printf("  • 分支页分裂：中间分隔符提升\n");
    printf("  • 根页分裂：树高度增加 1\n");
    printf("  • LMDB 使用 50/50 分裂（平衡）\n");
}

/**
 * 演示根分裂（树增长）
 */
void demonstrate_root_split() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   根节点分裂（树增长）                                    ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("场景：根页已满，需要分裂\n");
    printf("\n");

    printf("之前（深度 2）：\n");
    printf("  " COLOR_CYAN "Root (Page 2) - 叶页" COLOR_RESET "\n");
    printf("  [apple, banana, cherry, date, fig]\n");
    printf("\n");

    printf("插入 'grape'（已满）\n");
    printf("\n");

    printf("Step 1: 分裂根页\n");
    printf("  " COLOR_GREEN "Page 3 (左叶)" COLOR_RESET ": [apple, banana]\n");
    printf("  " COLOR_GREEN "Page 4 (右叶)" COLOR_RESET ": [date, fig]\n");
    printf("  提升键: 'cherry'\n");
    printf("\n");

    printf("Step 2: 创建新根页\n");
    printf("  " COLOR_YELLOW "分配新根 Page 5" COLOR_RESET "\n");
    printf("  " COLOR_CYAN "Page 5 (新根) - 分支页" COLOR_RESET "\n");
    printf("  [cherry]\n");
    printf("    ├─── Page 3: [apple, banana]\n");
    printf("    └─── Page 4: [date, fig]\n");
    printf("\n");

    printf("之后（深度 3）：\n");
    printf("  树高度增加了！\n");

    printf("\n" COLOR_YELLOW "关键点：" COLOR_RESET "\n");
    printf("  • 根分裂是树增长的唯一方式\n");
    printf("  • 新根包含 1 个键（分隔符）\n");
    printf("  • 所有叶页保持在同一深度\n");
    printf("  • B+ 树始终平衡\n");
}

/**
 * 演示页面重用（写时复制）
 */
void demonstrate_cow_reuse() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   写时复制与页面重用                                      ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("写时复制 (Copy-on-Write) 详解：\n");
    printf("\n");

    printf("初始状态：\n");
    printf("  " COLOR_GREEN "Page 10" COLOR_RESET " (版本 1): [apple, banana, cherry]\n");
    printf("  " COLOR_GREEN "读者 A" COLOR_RESET " 正在读取 Page 10 (txn_id: 100)\n");
    printf("\n");

    printf("修改操作：更新 'banana' → 'BANANA'\n");
    printf("\n");

    printf("Step 1: 分配新页\n");
    printf("  " COLOR_YELLOW "分配 Page 25" COLOR_RESET "\n");
    printf("\n");

    printf("Step 2: 复制并修改\n");
    printf("  " COLOR_GREEN "Page 25" COLOR_RESET " (版本 2): [apple, " COLOR_BOLD "BANANA" COLOR_RESET ", cherry]\n");
    printf("\n");

    printf("Step 3: 更新 B+ 树指针\n");
    printf("  父页中的指针: Page 10 → Page 25\n");
    printf("\n");

    printf("Step 4: 提交元数据\n");
    printf("  新事务 ID: 101\n");
    printf("  新的读者看到 Page 25\n");
    printf("\n");

    printf("页面状态：\n");
    printf("  " COLOR_GREEN "Page 10" COLOR_RESET ": [apple, banana, cherry]\n");
    printf("    → " COLOR_CYAN "读者 A 仍可见" COLOR_RESET " (txn_id: 100)\n");
    printf("  " COLOR_GREEN "Page 25" COLOR_RESET ": [apple, BANANA, cherry]\n");
    printf("    → " COLOR_YELLOW "新读者可见" COLOR_RESET " (txn_id: >= 101)\n");
    printf("\n");

    printf("Step 5: 读者 A 完成后\n");
    printf("  " COLOR_GREEN "Page 10" COLOR_RESET " 可以被释放\n");
    printf("  添加到空闲列表: [(Page 10, txn_id: 100)]\n");

    printf("\n" COLOR_YELLOW "页面重用条件：" COLOR_RESET "\n");
    printf("  页面可以被重用，当：\n");
    printf("  1. 页面已过时（不是当前版本）\n");
    printf("  2. 所有旧于该页面的读者已完成\n");
    printf("\n");
    printf("  示例：\n");
    printf("    空闲列表: [(Page 10, txn:100), (Page 15, txn:105)]\n");
    printf("    当前事务: 110\n");
    printf("    最老读者: 102\n");
    printf("    可重用: Page 10 (100 < 102) ✓\n");
    printf("    不可重用: Page 15 (105 >= 102) ✗\n");
}

/**
 * 演示节点合并
 */
void demonstrate_merge_process() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   B+ 树节点合并                                            ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("场景：删除键后页面利用率过低\n");
    printf("\n");

    printf("初始状态（删除前）：\n");
    printf("  " COLOR_CYAN "父页 (Page 2)" COLOR_RESET "\n");
    printf("    [cherry]\n");
    printf("      ├─── " COLOR_GREEN "Page 10 (叶)" COLOR_RESET ": [apple, banana, cherry]\n");
    printf("      └─── " COLOR_GREEN "Page 20 (叶)" COLOR_RESET ": [date, elderberry]\n");
    printf("\n");

    printf("删除操作：从 Page 10 删除所有键\n");
    printf("\n");

    printf("之后：\n");
    printf("  " COLOR_CYAN "父页 (Page 2)" COLOR_RESET "\n");
    printf("    [cherry]\n");
    printf("      ├─── " COLOR_RED "Page 10 (叶)" COLOR_RESET ": [] " COLOR_YELLOW "(空!)" COLOR_RESET "\n");
    printf("      └─── " COLOR_GREEN "Page 20 (叶)" COLOR_RESET ": [date, elderberry]\n");
    printf("\n");

    printf(COLOR_YELLOW "LMDB 的处理：" COLOR_RESET "\n");
    printf("  LMDB " COLOR_BOLD "不执行合并" COLOR_RESET "！原因：\n");
    printf("  1. 合并需要在父页中删除键\n");
    printf("  2. 可能级联向上传播\n");
    printf("  3. 写时复制使合并代价高昂\n");
    printf("  4. 性能影响超过收益\n");
    printf("\n");

    printf("实际策略：\n");
    printf("  • 空页添加到空闲列表\n");
    printf("  • 空间由未来的插入重用\n");
    printf("  • 页面不会立即归还给文件系统\n");
    printf("  • 空间回收由 free list 管理\n");
}

/**
 * 演示溢出页链
 */
void demonstrate_overflow_pages() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   溢出页链（大值存储）                                    ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("场景：存储一个 10KB 的值\n");
    printf("  (页面大小 4KB，值 > 页面大小的一半)\n");
    printf("\n");

    printf("Step 1: 检测值太大\n");
    printf("  值大小: 10,000 bytes\n");
    printf("  页面大小: 4,096 bytes\n");
    printf("  阈值: ~2,048 bytes (页面的一半)\n");
    printf("  → 需要溢出页!\n");
    printf("\n");

    printf("Step 2: 分配溢出页链\n");
    printf("  需要 3 个页面: ceil(10000 / 4096) = 3\n");
    printf("  " COLOR_YELLOW "分配 Page 100, 101, 102" COLOR_RESET "\n");
    printf("\n");

    printf("Step 3: 存储数据\n");
    printf("  " COLOR_GREEN "Page 100" COLOR_RESET ": [0-4095 字节]  → next: Page 101\n");
    printf("  " COLOR_GREEN "Page 101" COLOR_RESET ": [4096-8191 字节] → next: Page 102\n");
    printf("  " COLOR_GREEN "Page 102" COLOR_RESET ": [8192-9999 字节] → next: NULL\n");
    printf("\n");

    printf("Step 4: 叶页中的节点\n");
    printf("  " COLOR_CYAN "叶页 (Page 10)" COLOR_RESET "\n");
    printf("    [\"large_key\"] → (flags: F_BIGDATA, pgno: 100)\n");
    printf("\n");

    printf("访问大值：\n");
    printf("  1. 查找键 → 得到 pgno: 100\n");
    printf("  2. 读取 Page 100 → 得到数据和下一页指针\n");
    printf("  3. 读取 Page 101 → 继续...\n");
    printf("  4. 读取 Page 102 → 完成\n");
    printf("\n");

    printf(COLOR_YELLOW "溢出页特点：" COLOR_RESET "\n");
    printf("  • 链式存储（不是 B+ 树结构）\n");
    printf("  • 每页满载（无内部碎片）\n");
    printf("  • 只修改时才复制（COW）\n");
    printf("  • 删除时整个链释放\n");
}

/**
 * 实际演示
 */
void demonstrate_real_btree() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   实际 B+ 树结构查看                                        ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;

    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024 * 10);
    mdb_env_open(env, "./btree_demo_db", 0, 0664);

    printf("\n创建 B+ 树并插入数据...\n");

    mdb_txn_begin(env, NULL, 0, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);

    /* 插入足够的数据导致树增长 */
    const char* keys[] = {
        "apple", "apricot", "avocado", "banana", "blackberry",
        "blueberry", "cherry", "coconut", "cranberry", "date",
        "dragonfruit", "elderberry", "fig", "grape", "grapefruit",
        "guava", "honeydew", "kiwi", "lemon", "lime"
    };

    for (int i = 0; i < 20; i++) {
        MDB_val k = { .mv_size = strlen(keys[i]), .mv_data = (void*)keys[i] };
        MDB_val v = { .mv_size = 20, .mv_data = (void*)"fruit description" };
        mdb_put(txn, dbi, &k, &v, 0);
    }

    mdb_txn_commit(txn);

    /* 查看树结构 */
    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);

    MDB_stat stat;
    mdb_stat(txn, dbi, &stat);

    printf("\nB+ 树统计：\n");
    printf("  树深度: %u\n", stat.ms_depth);
    printf("  分支页: %zu\n", stat.ms_branch_pages);
    printf("  叶页: %zu\n", stat.ms_leaf_pages);
    printf("  溢出页: %zu\n", stat.ms_overflow_pages);
    printf("  条目数: %zu\n", stat.ms_entries);

    mdb_txn_abort(txn);
    mdb_dbi_close(env, dbi);
    mdb_env_close(env);

    printf("\n" COLOR_YELLOW "推断结构：" COLOR_RESET "\n");
    if (stat.ms_depth == 1) {
        printf("  只有根页（叶页），所有键在一页中\n");
    } else if (stat.ms_depth == 2) {
        printf("  根页是分支页，直接指向叶页\n");
        printf("  结构: Root → Leaf Pages\n");
    } else if (stat.ms_depth == 3) {
        printf("  根页 → 中间层 → 叶页\n");
        printf("  结构: Root → Branch → Leaf Pages\n");
    }
}

int main() {
    printf(COLOR_CYAN "\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     LMDB B+ 树实现深度解析                                 ║\n");
    printf("║     B+ Tree Implementation Deep Dive                      ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf(COLOR_RESET);

    demonstrate_search_algorithm();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 继续..." COLOR_RESET);
    getchar();

    demonstrate_split_process();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 继续..." COLOR_RESET);
    getchar();

    demonstrate_root_split();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 继续..." COLOR_RESET);
    getchar();

    demonstrate_cow_reuse();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 继续..." COLOR_RESET);
    getchar();

    demonstrate_merge_process();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 继续..." COLOR_RESET);
    getchar();

    demonstrate_overflow_pages();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 继续..." COLOR_RESET);
    getchar();

    demonstrate_real_btree();

    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   总结                                                    ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf(COLOR_YELLOW "B+ 树操作复杂度：" COLOR_RESET "\n");
    printf("  搜索:   O(log n)    - 从根到叶\n");
    printf("  插入:   O(log n)    - 搜索 + 分裂\n");
    printf("  删除:   O(log n)    - 搜索 + 标记删除\n");
    printf("  范围扫描: O(k + log n) - k 是结果数量\n");

    printf("\n" COLOR_YELLOW "LMDB 特点：" COLOR_RESET "\n");
    printf("  1. 写时复制 - 修改分配新页\n");
    printf("  2. 不合并 - 删除留空，以后重用\n");
    printf("  3. 平衡 - 所有叶页同深度\n");
    printf("  4. 分裂策略 - 50/50 分裂\n");

    printf(COLOR_GREEN "\n✓ btree_impl 完成!\n" COLOR_RESET);
    printf("  数据库: ./btree_demo_db/\n");

    return 0;
}
