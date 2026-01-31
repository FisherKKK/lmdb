/**
 * code_explorer.c - LMDB 源代码导航助手
 *
 * 这个程序帮助理解 mdb.c 的源代码结构和关键函数。
 *
 * 学习目标：
 * - 理解 mdb.c 的文件组织
 * - 知道关键函数在哪里
 * - 理解函数调用关系
 * - 学会如何阅读源代码
 */

#include <stdio.h>
#include <string.h>

#define COLOR_RESET   "\033[0m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_RED     "\033[31m"
#define COLOR_BLUE    "\033[34m"

/**
 * mdb.c 源代码结构总览
 */
void show_source_structure() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   mdb.c 源代码结构                                         ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("mdb.c 约 11,500 行代码，组织如下：\n");
    printf("\n");

    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│  1. 头文件和常量定义         (行 1-500)                  │\n");
    printf("│  2. 数据结构定义               (行 500-1500)                │\n");
    printf("│  3. 内存管理函数               (行 1500-2500)              │\n");
    printf("│  4. 页面操作函数               (行 2500-4000)              │\n");
    printf("│  5. B+ 树核心函数             (行 4000-6500)              │\n");
    printf("│  6. 事务管理函数             (行 6500-8000)              │\n");
    printf("│  7. 游标操作函数               (行 8000-9500)              │\n");
    printf("│  8. 工具函数                     (行 9500-11000)             │\n");
    printf("│  9. 平台特定代码               (行 11000-11500)           │\n");
    printf("└─────────────────────────────────────────────────────────┘\n");
    printf("\n");

    printf("关键宏定义：\n");
    printf("  " COLOR_GREEN "DEBUG" COLOR_RESET "  - 启用调试输出\n");
    printf("  " COLOR_GREEN "MDB_DEBUG" COLOR_RESET " - 额外的断言检查\n");
    printf("  " COLOR_GREEN "MDB_PAGESIZE" COLOR_RESET " - 默认页面大小 (4096)\n");
    printf("  " COLOR_GREEN "MDB_MINKEYS" COLOR_RESET " - 最小键数 (通常 2)\n");
    printf("  " COLOR_GREEN "MDB_MAXKEYS" COLOR_RESET " - 最大键数 (基于页大小)\n");
}

/**
 * 显示关键数据结构
 */
void show_key_structures() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   关键数据结构及其位置                                     ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    typedef struct {
        const char* name;
        const char* location;
        const char* description;
    } structure_t;

    structure_t structs[] = {
        {"MDB_env",   "约行 1000",  "环境句柄，包含整个数据库状态"},
        {"MDB_txn",   "约行 1200",  "事务句柄，跟踪读/写事务状态"},
        {"MDB_cursor","约行 1400",  "游标，用于遍历 B+ 树"},
        {"MDB_page",  "约行 800",   "页面结构，数据库的基本单位"},
        {"MDB_node",  "约行 900",   "节点结构，页面内的条目"},
        {"MDB_db",    "约行 1100",  "数据库句柄，指向根页"},
        {NULL, NULL, NULL}
    };

    printf("┌──────────────────┬─────────────┬────────────────────────────┐\n");
    printf("│ 结构体            │ 位置        │ 描述                       │\n");
    printf("├──────────────────┼─────────────┼────────────────────────────┤\n");

    for (int i = 0; structs[i].name != NULL; i++) {
        printf("│ " COLOR_GREEN "%-16s" COLOR_RESET " │ %-11s │ %-26s │\n",
               structs[i].name, structs[i].location, structs[i].description);
    }

    printf("└──────────────────┴─────────────┴────────────────────────────┘\n");
}

/**
 * 显示关键函数
 */
void show_key_functions() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   关键函数及其位置                                       ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    typedef struct {
        const char* name;
        const char* location;
        const char* purpose;
    } function_t;

    function_t env_funcs[] = {
        {"mdb_env_create",    "约行 7000", "创建环境句柄"},
        {"mdb_env_open",      "约行 7200", "打开/创建数据库"},
        {"mdb_env_close",     "约行 7400", "关闭环境"},
        {"mdb_env_set_mapsize", "约行 7100", "设置映射大小"},
        {NULL, NULL, NULL}
    };

    function_t txn_funcs[] = {
        {"mdb_txn_begin",     "约行 7500", "开始事务"},
        {"mdb_txn_commit",    "约行 7700", "提交事务"},
        {"mdb_txn_abort",     "约行 7800", "中止事务"},
        {"mdb_txn_renew",     "约行 7600", "续期只读事务"},
        {NULL, NULL, NULL}
    };

    function_t db_funcs[] = {
        {"mdb_dbi_open",      "约行 8200", "打开数据库"},
        {"mdb_dbi_close",     "约行 8300", "关闭数据库"},
        {"mdb_stat",          "约行 9500", "获取统计信息"},
        {NULL, NULL, NULL}
    };

    function_t crud_funcs[] = {
        {"mdb_get",           "约行 8500", "读取数据"},
        {"mdb_put",           "约行 8600", "写入数据"},
        {"mdb_del",           "约行 8800", "删除数据"},
        {NULL, NULL, NULL}
    };

    function_t cursor_funcs[] = {
        {"mdb_cursor_open",   "约行 9000", "打开游标"},
        {"mdb_cursor_close",  "约行 9100", "关闭游标"},
        {"mdb_cursor_get",    "约行 9200", "游标操作"},
        {"mdb_cursor_put",    "约行 9300", "游标写入"},
        {NULL, NULL, NULL}
    };

    function_t internal_funcs[] = {
        {"mdb_page_get",      "约行 3000", "获取页面（内部）"},
        {"mdb_page_search",   "约行 4500", "搜索页面（内部）"},
        {"mdb_page_touch",    "约行 3200", "标记页面脏（内部）"},
        {"mdb_page_alloc",    "约行 2800", "分配页面（内部）"},
        {"mdb_node_search",   "约行 4700", "搜索节点（内部）"},
        {NULL, NULL, NULL}
    };

    printf("\n" COLOR_YELLOW "环境管理:" COLOR_RESET "\n");
    printf("┌────────────────────────┬─────────────┬────────────────────┐\n");
    printf("│ 函数                   │ 位置        │ 用途               │\n");
    printf("├────────────────────────┼─────────────┼────────────────────┤\n");
    for (int i = 0; env_funcs[i].name != NULL; i++) {
        printf("│ " COLOR_GREEN "%-22s" COLOR_RESET " │ %-11s │ %-18s │\n",
               env_funcs[i].name, env_funcs[i].location, env_funcs[i].purpose);
    }
    printf("└────────────────────────┴─────────────┴────────────────────┘\n");

    printf("\n" COLOR_YELLOW "事务管理:" COLOR_RESET "\n");
    printf("┌────────────────────────┬─────────────┬────────────────────┐\n");
    for (int i = 0; txn_funcs[i].name != NULL; i++) {
        printf("│ " COLOR_GREEN "%-22s" COLOR_RESET " │ %-11s │ %-18s │\n",
               txn_funcs[i].name, txn_funcs[i].location, txn_funcs[i].purpose);
    }
    printf("└────────────────────────┴─────────────┴────────────────────┘\n");

    printf("\n" COLOR_YELLOW "数据操作:" COLOR_RESET "\n");
    printf("┌────────────────────────┬─────────────┬────────────────────┐\n");
    for (int i = 0; crud_funcs[i].name != NULL; i++) {
        printf("│ " COLOR_GREEN "%-22s" COLOR_RESET " │ %-11s │ %-18s │\n",
               crud_funcs[i].name, crud_funcs[i].location, crud_funcs[i].purpose);
    }
    printf("└────────────────────────┴─────────────┴────────────────────┘\n");

    printf("\n" COLOR_YELLOW "游标操作:" COLOR_RESET "\n");
    printf("┌────────────────────────┬─────────────┬────────────────────┐\n");
    for (int i = 0; cursor_funcs[i].name != NULL; i++) {
        printf("│ " COLOR_GREEN "%-22s" COLOR_RESET " │ %-11s │ %-18s │\n",
               cursor_funcs[i].name, cursor_funcs[i].location, cursor_funcs[i].purpose);
    }
    printf("└────────────────────────┴─────────────┴────────────────────┘\n");

    printf("\n" COLOR_YELLOW "内部函数（理解实现的关键）:" COLOR_RESET "\n");
    printf("┌────────────────────────┬─────────────┬────────────────────┐\n");
    for (int i = 0; internal_funcs[i].name != NULL; i++) {
        printf("│ " COLOR_CYAN "%-22s" COLOR_RESET " │ %-11s │ %-18s │\n",
               internal_funcs[i].name, internal_funcs[i].location, internal_funcs[i].purpose);
    }
    printf("└────────────────────────┴─────────────┴────────────────────┘\n");
}

/**
 * 显示函数调用流程
 */
void show_call_flows() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   关键操作调用流程                                        ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    printf("\n" COLOR_YELLOW "1. 读取操作 (mdb_get):" COLOR_RESET "\n");
    printf("  │\n");
    printf("  ├─" COLOR_GREEN "mdb_get(txn, dbi, key, data)" COLOR_RESET "\n");
    printf("  │   │\n");
    printf("  │   ├─► 检查参数\n");
    printf("  │   │\n");
    printf("  │   ├─► " COLOR_CYAN "mdb_cursor_open(txn, dbi, &cursor)" COLOR_RESET "\n");
    printf("  │   │\n");
    printf("  │   ├─► " COLOR_CYAN "mdb_cursor_get(cursor, key, data, MDB_SET)" COLOR_RESET "\n");
    printf("  │   │   │\n");
    printf("  │   │   └─► " COLOR_CYAN "mdb_page_search(root, key)" COLOR_RESET "\n");
    printf("  │   │       │\n");
    printf("  │   │       ├─► 从根页开始\n");
    printf("  │   │       │\n");
    printf("  │   │       ├─► while (是分支页) {\n");
    printf("  │   │       │      node = mdb_node_search(page, key);\n");
    printf("  │   │       │      page = mdb_page_get(node->pgno);\n");
    printf("  │   │       │    }\n");
    printf("  │   │       │\n");
    printf("  │   │       └─► 返回叶页节点\n");
    printf("  │   │\n");
    printf("  │   └─► " COLOR_CYAN "mdb_cursor_close(cursor)" COLOR_RESET "\n");
    printf("  │\n");
    printf("  └─► 返回数据指针（直接指向内存映射）\n");

    printf("\n" COLOR_YELLOW "2. 写入操作 (mdb_put):" COLOR_RESET "\n");
    printf("  │\n");
    printf("  ├─" COLOR_GREEN "mdb_put(txn, dbi, key, data, flags)" COLOR_RESET "\n");
    printf("  │   │\n");
    printf("  │   ├─► 检查是只读事务？\n");
    printf("  │   │\n");
    printf("  │   ├─► " COLOR_CYAN "mdb_cursor_open(txn, dbi, &cursor)" COLOR_RESET "\n");
    printf("  │   │\n");
    printf("  │   ├─► " COLOR_CYAN "mdb_cursor_put(cursor, key, data, flags)" COLOR_RESET "\n");
    printf("  │   │   │\n");
    printf("  │   │   ├─► 搜索键的位置\n");
    printf("  │   │   │   └─► mdb_page_search()\n");
    printf("  │   │   │\n");
    printf("  │   │   ├─► 键已存在？\n");
    printf("  │   │   │   ├─ 是 → 更新值（标记页面脏）\n");
    printf("  │   │   │   └─ 否 → 继续插入\n");
    printf("  │   │   │\n");
    printf("  │   │   ├─► 插入新节点\n");
    printf("  │   │   │   ├─► " COLOR_CYAN "mdb_page_touch(page)" COLOR_RESET " - COW\n");
    printf("  │   │   │   ├─► 在页面中分配空间\n");
    printf("  │   │   │   ├─► 复制键和值\n");
    printf("  │   │   │   └─► 更新页面指针数组\n");
    printf("  │   │   │\n");
    printf("  │   │   ├─► 页面满？\n");
    printf("  │   │   │   └─► " COLOR_CYAN "mdb_page_split(page)" COLOR_RESET "\n");
    printf("  │   │   │       ├─► 分配新页\n");
    printf("  │   │   │       ├─► 复制键到两页\n");
    printf("  │   │   │       ├─► 提升中间键到父页\n");
    printf("  │   │   │       └─► 父页满？递归分裂\n");
    printf("  │   │   │\n");
    printf("  │   │   └─► 返回\n");
    printf("  │   │\n");
    printf("  │   └─► " COLOR_CYAN "mdb_cursor_close(cursor)" COLOR_RESET "\n");
    printf("  │\n");
    printf("  └─► 返回状态\n");

    printf("\n" COLOR_YELLOW "3. 事务提交 (mdb_txn_commit):" COLOR_RESET "\n");
    printf("  │\n");
    printf("  ├─" COLOR_GREEN "mdb_txn_commit(txn)" COLOR_RESET "\n");
    printf("  │   │\n");
    printf("  │   ├─► 是否嵌套事务？\n");
    printf("  │   │   ├─ 是 → 合并到父事务\n");
    printf("  │   │   └─ 否 → 继续提交\n");
    printf("  │   │\n");
    printf("  │   ├─► " COLOR_CYAN "mdb_env_sync(env, flag)" COLOR_RESET "\n");
    printf("  │   │   │\n");
    printf("  │   │   ├─► 刷新脏页到磁盘\n");
    printf("  │   │   │\n");
    printf("  │   │   ├─► 更新元数据页\n");
    printf("  │   │   │   ├─► 递增事务 ID\n");
    printf("  │   │   │   ├─► 更新根页指针\n");
    printf("  │   │   │   └─► 交替写入 page 0 或 page 1\n");
    printf("  │   │   │\n");
    printf("  │   │   └─► msync()/fsync()\n");
    printf("  │   │\n");
    printf("  │   ├─► 通知等待的写入者\n");
    printf("  │   │\n");
    printf("  │   └─► 释放事务资源\n");
    printf("  │\n");
    printf("  └─► 返回 MDB_SUCCESS\n");

    printf("\n" COLOR_YELLOW "4. 页面分配 (mdb_page_alloc):" COLOR_RESET "\n");
    printf("  │\n");
    printf("  ├─" COLOR_CYAN "mdb_page_alloc(txn, num)" COLOR_RESET "\n");
    printf("  │   │\n");
    printf("  │   ├─► 检查空闲列表\n");
    printf("  │   │   ├─ 有可用页面？\n");
    printf("  │   │   │   └─► 从空闲列表取\n");
    printf("  │   │   └─ 无可用页面？\n");
    printf("  │   │       └─► 扩展数据库文件\n");
    printf("  │   │           ├─► 增加文件大小\n");
    printf("  │   │           └─► 重新映射 (mremap)\n");
    printf("  │   │\n");
    printf("  │   ├─► 返回页面号\n");
    printf("  │   │\n");
    printf("  │   └─► 标记页面为脏\n");
    printf("  │\n");
    printf("  └─► 返回页面指针 (me_map + pgno * PAGESIZE)\n");
}

/**
 * 学习建议
 */
void show_study_tips() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   源代码学习建议                                          ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf(COLOR_YELLOW "推荐的阅读顺序：" COLOR_RESET "\n");
    printf("\n");
    printf("  " COLOR_GREEN "第一阶段：理解数据结构" COLOR_RESET "\n");
    printf("    1. 从行 500 开始，阅读 MDB_txn, MDB_cursor, MDB_page 定义\n");
    printf("    2. 理解页面布局 (mp_pgno, mp_flags, mp_lower, mp_upper)\n");
    printf("    3. 理解节点结构 (ksize, vsize, 数据)\n");
    printf("\n");

    printf("  " COLOR_GREEN "第二阶段：理解基本操作" COLOR_RESET "\n");
    printf("    1. mdb_page_get() - 如何获取页面\n");
    printf("    2. mdb_node_search() - 如何在页中搜索\n");
    printf("    3. mdb_page_search() - 如何搜索 B+ 树\n");
    printf("\n");

    printf("  " COLOR_GREEN "第三阶段：理解写操作" COLOR_RESET "\n");
    printf("    1. mdb_page_touch() - 写时复制\n");
    printf("    2. mdb_page_split() - 页面分裂\n");
    printf("    3. mdb_node_add() - 添加节点\n");
    printf("\n");

    printf("  " COLOR_GREEN "第四阶段：理解事务" COLOR_RESET "\n");
    printf("    1. mdb_txn_begin() - 如何开始事务\n");
    printf("    2. mdb_txn_commit() - 如何提交\n");
    printf("    3. 读者表管理\n");
    printf("\n");

    printf("  " COLOR_GREEN "第五阶段：平台特定代码" COLOR_RESET "\n");
    printf("    1. 内存映射实现 (mmap vs Windows)\n");
    printf("    2. 同步原语 (mutex, semaphore)\n");
    printf("    3. 错误处理\n");

    printf("\n" COLOR_YELLOW "调试技巧：" COLOR_RESET "\n");
    printf("  1. 编译时启用调试：\n");
    printf("     make CFLAGS=\"-DMD_DEBUG=1\"\n");
    printf("\n");
    printf("  2. 使用 GDB 设置断点：\n");
    printf("     gdb ./your_program\n");
    printf("     (gdb) break mdb_txn_begin\n");
    printf("     (gdb) run\n");
    printf("     (gdb) print *txn\n");
    printf("\n");
    printf("  3. 使用 valgrind 检测内存问题：\n");
    printf("     valgrind --leak-check=full ./your_program\n");

    printf("\n" COLOR_YELLOW "关键概念对照：" COLOR_RESET "\n");
    printf("  ┌────────────────────┬────────────────────────────────────┐\n");
    printf("  │ 概念                │ 源代码中的体现                      │\n");
    printf("  ├────────────────────┼────────────────────────────────────┤\n");
    printf("  │ " COLOR_GREEN "内存映射" COLOR_RESET "          │ me_map, mdb_env_get_page()        │\n");
    printf("  │ " COLOR_GREEN "写时复制" COLOR_RESET "          │ mdb_page_touch(), COW             │\n");
    printf("  │ " COLOR_GREEN "B+ 树" COLOR_RESET "              │ MDB_page, MDB_node               │\n");
    printf("  │ " COLOR_GREEN "MVCC" COLOR_RESET "               │ 读者表, txn_id                   │\n");
    printf("  │ " COLOR_GREEN "事务" COLOR_RESET "              │ MDB_txn, mt_u                    │\n");
    printf("  └────────────────────┴────────────────────────────────────┘\n");
}

/**
 * 显示常用宏定义
 */
void show_key_macros() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   关键宏定义                                               ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("页面相关宏 (约行 400-600):\n");
    printf("  #define PAGETYPE(p)     (((p)->mp_flags) & 0xFF)\n");
    printf("  #define IS_BRANCH(p)    (PAGETYPE(p) == P_BRANCH)\n");
    printf("  #define IS_LEAF(p)      (PAGETYPE(p) == P_LEAF)\n");
    printf("  #define PAGEHDRSZ       (sizeof MDB_page)  // 页首大小\n");
    printf("  #define PAGEBASE(p)     ((MDB_page *)(p))      // 页基址\n");
    printf("  #define METADATA(p)     ((void *)((char *)(p) + PAGEHDRSZ))\n");
    printf("\n");

    printf("节点相关宏 (约行 700-800):\n");
    printf("  #define NODESIZE         offsetof(MDB_node, mn_data)\n");
    printf("  #define NODEKEY(node)    ((void *)((node)->mn_data))\n");
    printf("  #define NODEDATA(node)   ((void *)((char *)(node)->mn_data + \\\n");
    printf("                           (node)->mn_ksize))\n");
    printf("\n");

    printf("调试宏 (约行 200-300):\n");
    printf("  #define DPRINTF(x)      if (mdb_debug) printf x\n");
    printf("  #define DPUTS(x)        if (mdb_debug) mdb_audit x\n");
    printf("\n");

    printf("错误处理宏 (约行 100-150):\n");
    printf("  #define MDB_ERR_BEGIN(loop) \\\n");
    printf("    int rc, mdb_rc = 0; \\\n");
    printf("    loop\n");
    printf("  #define EINVALID(err) \\\n");
    printf("    ((rc = err) != MDB_SUCCESS)\n");
}

int main() {
    printf(COLOR_CYAN "\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     LMDB 源代码导航助手                                    ║\n");
    printf("║     Source Code Navigation Guide                          ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf(COLOR_RESET);

    printf("\n这个工具帮助你理解 mdb.c 的结构和组织。\n");
    printf("mdb.c 是 LMDB 的核心，约 11,500 行代码。\n");

    show_source_structure();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 继续..." COLOR_RESET);
    getchar();

    show_key_structures();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 继续..." COLOR_RESET);
    getchar();

    show_key_functions();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 继续..." COLOR_RESET);
    getchar();

    show_key_macros();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 继续查看调用流程..." COLOR_RESET);
    getchar();

    show_call_flows();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 查看学习建议..." COLOR_RESET);
    getchar();

    show_study_tips();

    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   下一步                                                  ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("现在你可以：\n");
    printf("  1. 打开 libraries/liblmdb/mdb.c\n");
    printf("  2. 跳到感兴趣的函数\n");
    printf("  3. 使用本指南作为参考\n");
    printf("\n");

    printf(COLOR_GREEN "推荐的第一个函数：" COLOR_RESET "\n");
    printf("  mdb_page_search() - B+ 树搜索的核心\n");
    printf("  位置：约行 4500-4700\n");
    printf("\n");

    printf("推荐工具：\n");
    printf("  " COLOR_YELLOW "grep -n 'mdb_page_search' mdb.c" COLOR_RESET "  - 查找行号\n");
    printf("  " COLOR_YELLOW "less mdb.c" COLOR_RESET "                    - 浏览代码\n");
    printf("  " COLOR_YELLOW "ctags" COLOR_RESET "                          - 生成标签\n");

    printf(COLOR_GREEN "\n✓ code_explorer 完成!\n" COLOR_RESET);

    return 0;
}
