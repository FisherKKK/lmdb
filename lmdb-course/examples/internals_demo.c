/**
 * internals_demo.c - LMDB Internal Data Structures Deep Dive
 *
 * 这个程序深入演示 LMDB 的核心数据结构，帮助理解底层实现。
 *
 * 学习目标：
 * - 理解 MDB_env 环境结构
 * - 理解 MDB_txn 事务结构
 * - 理解 MDB_cursor 游标结构
 * - 理解页面节点结构
 * - 看到这些结构在内存中的布局
 *
 * 注意：这个程序访问 LMDB 内部结构，仅供学习使用。
 * 生产代码应该只使用公共 API。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <lmdb.h>

#define COLOR_RESET   "\033[0m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

/**
 * 演示 LMDB 环境的内存布局
 */
void demonstrate_env_layout() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   MDB_env 内部结构（简化版）                              ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("MDB_env 结构包含以下关键字段：\n");
    printf("\n");

    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│  typedef struct MDB_env {                               │\n");
    printf("│      /* 文件和映射相关 */                                │\n");
    printf("│      int     me_fd;            // 数据库文件描述符       │\n");
    printf("│      int     me_lfd;           // 锁文件描述符           │\n");
    printf("│      void*   me_map;           // 内存映射基址           │\n");
    printf("│      size_t  me_mapsize;       // 映射大小               │\n");
    printf("│                                                          │\n");
    printf("│      /* 事务管理 */                                       │\n");
    printf("│      MDB_txn* me_txn;          // 当前写事务             │\n");
    printf("│      MDB_txn* me_txn0;         // 只读事务链表           │\n");
    printf("│                                                          │\n");
    printf("│      /* 页面管理 */                                       │\n");
    printf("│      pgno_t  me_last_pg;       // 最后使用的页面号       │\n");
    printf("│      pgno_t* me_free_pgs;      // 空闲页面列表           │\n");
    printf("│                                                          │\n");
    printf("│      /* 元数据 */                                         │\n");
    printf("│      MDB_db  me_dbs[2];         // 主数据库和自由数据库   │\n");
    printf("│      uint16_t me_numdbs;       // 命名数据库数量         │\n");
    printf("│                                                          │\n");
    printf("│      /* 配置参数 */                                       │\n");
    printf("│      unsigned int me_maxreaders; // 最大读者数           │\n");
    printf("│      unsigned int me_maxdbs;    // 最大数据库数          │\n");
    printf("│      unsigned int me_flags;     // 环境标志             │\n");
    printf("│  } MDB_env;                                               │\n");
    printf("└─────────────────────────────────────────────────────────┘\n");
    printf("\n");

    printf(COLOR_YELLOW "关键点：" COLOR_RESET "\n");
    printf("  1. me_map 指向整个数据库文件的内存映射\n");
    printf("  2. 所有页面通过 me_map 基址 + 偏移量访问\n");
    printf("  3. me_txn0 是只读事务的链表头\n");
    printf("  4. me_free_pgs 跟踪可重用的页面\n");
}

/**
 * 演示事务结构
 */
void demonstrate_txn_layout() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   MDB_txn 事务结构（简化版）                              ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│  typedef struct MDB_txn {                               │\n");
    printf("│      /* 上下文 */                                         │\n");
    printf("│      MDB_txn*    mt_parent;     // 父事务（嵌套）       │\n");
    printf("│(" COLOR_GREEN "16 bytes" COLOR_RESET ") MDB_txn*    mt_next;       // 链表中的下一个        │\n");
    printf("│                                                          │\n");
    printf("│      /* 事务信息 */                                       │\n");
    printf("│(" COLOR_GREEN "16 bytes" COLOR_RESET ") size_t     mt_txnid;      // 事务 ID              │\n");
    printf("│(" COLOR_GREEN "16 bytes" COLOR_RESET ") unsigned   mt_flags;      // 只读/读写标志        │\n");
    printf("│                                                          │\n");
    printf("│      /* 环境和数据库 */                                   │\n");
    printf("│(" COLOR_GREEN "8 bytes" COLOR_RESET ")  MDB_env*   mt_env;        // 环境句柄             │\n");
    printf("│(" COLOR_GREEN "8 bytes" COLOR_RESET ")  MDB_db*    mt_dbs;        // 数据库句柄数组       │\n");
    printf("│                                                          │\n");
    printf("│      /* 页面跟踪 */                                       │\n");
    printf("│(" COLOR_GREEN "8 bytes" COLOR_RESET ")  pgno_t     mt_next_pgno;  // 下一个可用页面       │\n");
    printf("│(" COLOR_GREEN "8 bytes" COLOR_RESET ")  MDB_page*  mt_rpages;     // 释放的页面链表       │\n");
    printf("│                                                          │\n");
    printf("│      /* 读者槽位（仅只读事务）*/                          │\n");
    printf("│      void*       mt_u;           // 读者槽位指针         │\n");
    printf("│  } MDB_txn;                                               │\n");
    printf("└─────────────────────────────────────────────────────────┘\n");
    printf("\n");

    printf(COLOR_YELLOW "关键点：" COLOR_RESET "\n");
    printf("  1. " COLOR_GREEN "读事务" COLOR_RESET "：mt_parent = NULL，mt_u 指向读者槽位\n");
    printf("  2. " COLOR_GREEN "写事务" COLOR_RESET "：mt_parent = NULL 或父事务，mt_u = NULL\n");
    printf("  3. " COLOR_GREEN "嵌套事务" COLOR_RESET "：mt_parent 指向父事务\n");
    printf("  4. 事务 ID 单调递增，用于版本管理\n");
}

/**
 * 演示页面结构
 */
void demonstrate_page_layout() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   MDB_page 页面结构（实际内存布局）                        ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("每个页面 4096 字节（典型），布局如下：\n");
    printf("\n");

    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│  偏移   大小    字段                                    │\n");
    printf("├─────────────────────────────────────────────────────────┤\n");
    printf("│  +0     2 bytes  mp_pgno      页面号                     │\n");
    printf("│  +2     2 bytes  mp_flags     页面类型标志               │\n");
    printf("│                      0x02 = 分支页                        │\n");
    printf("│                      0x04 = 叶页                          │\n");
    printf("│                      0x05 = 溢出页                        │\n");
    printf("│  +4     2 bytes  mp_lower     下界（键从上往下增长）     │\n");
    printf("│  +6     2 bytes  mp_upper     上界（数据从下往上增长）   │\n");
    printf("│  +8     4 bytes  mp_pages     溢出页数量                 │\n");
    printf("│  +12    PAGE_SIZE-12  数据区（键和值）                   │\n");
    printf("└─────────────────────────────────────────────────────────┘\n");
    printf("\n");

    printf(COLOR_YELLOW "内存布局示意图：" COLOR_RESET "\n");
    printf("  页首 (12 bytes) │ 数据从上往下 │    自由空间    │ 数据从下往上 │\n");
    printf("                ▲                ▲                ▲            │\n");
    printf("                │                │                │            │\n");
    printf("              mp_lower       指针数组         mp_upper        │\n");
    printf("\n");

    printf("这种双向增长设计最大化空间利用率！\n");
}

/**
 * 演示节点结构
 */
void demonstrate_node_layout() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   MDB_node 页面内节点结构                                   ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("在分支页中（指向子页）：\n");
    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│  struct {                                              │\n");
    printf("│      uint16_t  ksize;      // 键大小                   │\n");
    printf("│(" COLOR_GREEN "2 bytes" COLOR_RESET ")      uint16_t  vsize;      // 值大小（= 页号）     │\n");
    printf("│(" COLOR_GREEN "4 bytes" COLOR_RESET ")      pgno_t    pgno;       // 子页面号             │\n");
    printf("│      // 键数据紧随其后                                   │\n");
    printf("│  } branch_node;                                          │\n");
    printf("└─────────────────────────────────────────────────────────┘\n");
    printf("\n");

    printf("在叶页中（实际数据）：\n");
    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│  struct {                                              │\n");
    printf("│      uint16_t  ksize;      // 键大小                   │\n");
    printf("│(" COLOR_GREEN "2 bytes" COLOR_RESET ")      uint16_t  vsize;      // 值大小               │\n");
    printf("│(" COLOR_GREEN "2 bytes" COLOR_RESET ")      uint16_t  flags;      // 标志                 │\n");
    printf("│      // 键数据紧随其后                                   │\n");
    printf("│      // 值数据紧随键之后                                 │\n");
    printf("│  } leaf_node;                                            │\n");
    printf("└─────────────────────────────────────────────────────────┘\n");
    printf("\n");

    printf(COLOR_YELLOW "特殊情况 - 大数据：" COLOR_RESET "\n");
    printf("  如果值 > 页面大小的一半：\n");
    printf("  - flags 设置为 F_BIGDATA\n");
    printf("  - 值是一个 pgno_t，指向溢出页链\n");
}

/**
 * 演示游标结构
 */
void demonstrate_cursor_layout() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   MDB_cursor 游标结构                                     ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("游标维护从根到当前位置的路径：\n");
    printf("\n");

    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│  typedef struct MDB_cursor {                            │\n");
    printf("│      MDB_txn*    mc_txn;        // 所属事务             │\n");
    printf("│      MDB_db*     mc_dbi;        // 数据库句柄           │\n");
    printf("│                                                          │\n");
    printf("│      /* 路径栈 */                                         │\n");
    printf("│      unsigned short mc_top;       // 栈顶索引           │\n");
    printf("│(" COLOR_GREEN "42 * 2 bytes" COLOR_RESET ")      MDB_xcursor  mc_xcursor;   // 路径栈数组     │\n");
    printf("│                                                          │\n");
    printf("│      /* 每个栈元素包含：*/                                 │\n");
    printf("│      struct {                                            │\n");
    printf("│          MDB_page*  mp;          // 页面指针             │\n");
    printf("│          unsigned short mn;       // 页面内节点索引       │\n");
    printf("│      } mc_stack[42];  // 最多 42 层（足以应付所有情况）  │\n");
    printf("│  } MDB_cursor;                                            │\n");
    printf("└─────────────────────────────────────────────────────────┘\n");
    printf("\n");

    printf(COLOR_YELLOW "路径栈示例：" COLOR_RESET "\n");
    printf("  查找 'banana' 时：\n");
    printf("  mc_stack[0]: 根页，节点 1           (banana > cherry)    │\n");
    printf("  mc_stack[1]: 分支页，节点 0        (banana < berry)     │\n");
    printf("  mc_stack[2]: 叶页，节点 3         (找到 banana)        │\n");
    printf("  mc_top = 2\n");
}

/**
 * 演示读者表结构
 */
void demonstrate_reader_table() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   读者表（Reader Table） - 锁文件中的核心结构             ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("读者表存储在锁文件（lock.mdb）中，跟踪所有活跃的读事务：\n");
    printf("\n");

    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│  typedef struct MDB_reader {                            │\n");
    printf("│      pid_t     mr_pid;         // 进程 ID                │\n");
    printf("│(" COLOR_GREEN "4 bytes" COLOR_RESET ")      pthread_t mr_tid;         // 线程 ID                │\n");
    printf("│(" COLOR_GREEN "8 bytes" COLOR_RESET ")      size_t    mr_txnid;       // 事务 ID               │\n");
    printf("│      void*     mr_txn;         // 事务指针                │\n");
    printf("│  } MDB_reader;                                            │\n");
    printf("└─────────────────────────────────────────────────────────┘\n");
    printf("\n");

    printf("锁文件布局：\n");
    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│  元数据 (page 0 和 page 1)                               │\n");
    printf("│  读者表 (固定大小数组，例如 126 个槽位)                   │\n");
    printf("│                                                          │\n");
    printf("│  槽位 0: {pid: 1234, tid: 1001, txnid: 50}  " COLOR_GREEN "(活跃)" COLOR_RESET "\n");
    printf("│  槽位 1: {pid: 1235, tid: 2001, txnid: 55}  " COLOR_GREEN "(活跃)" COLOR_RESET "\n");
    printf("│  槽位 2: {pid: 0,    tid: 0,    txnid: 0}    " COLOR_YELLOW "(空闲)" COLOR_RESET "\n");
    printf("│  ...                                                      │\n");
    printf("└─────────────────────────────────────────────────────────┘\n");
    printf("\n");

    printf(COLOR_YELLOW "关键用途：" COLOR_RESET "\n");
    printf("  1. 检测过期读者（进程崩溃留下的槽位）\n");
    printf("  2. 确定哪些页面可以安全释放\n");
    printf("  3. 页面释放条件：txnid < 所有活跃读者的最小 txnid\n");
}

/**
 * 实际查看数据库文件布局
 */
void examine_real_database() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   实际数据库文件布局                                       ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("创建一个示例数据库...\n");

    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;

    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024);
    mdb_env_open(env, "./internals_demo_db", 0, 0664);

    mdb_txn_begin(env, NULL, 0, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);

    /* 插入一些数据 */
    const char* keys[] = {"alpha", "beta", "gamma", "delta", "epsilon"};
    for (int i = 0; i < 5; i++) {
        MDB_val k = { .mv_size = strlen(keys[i]), .mv_data = (void*)keys[i] };
        MDB_val v = { .mv_size = 6, .mv_data = (void*)"value!" };
        mdb_put(txn, dbi, &k, &v, 0);
    }

    mdb_txn_commit(txn);

    /* 获取统计信息 */
    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);

    MDB_stat stat;
    mdb_stat(txn, dbi, &stat);

    printf("\n数据库统计：\n");
    printf("  页面大小:     %u bytes\n", stat.ms_psize);
    printf("  树深度:       %u\n", stat.ms_depth);
    printf("  分支页数:     %zu\n", stat.ms_branch_pages);
    printf("  叶页数:       %zu\n", stat.ms_leaf_pages);
    printf("  溢出页数:     %zu\n", stat.ms_overflow_pages);
    printf("  条目数:       %zu\n", stat.ms_entries);

    mdb_txn_abort(txn);
    mdb_dbi_close(env, dbi);
    mdb_env_close(env);

    printf("\n" COLOR_YELLOW "文件布局（简化）：" COLOR_RESET "\n");
    printf("  Page 0: 元数据页 (Meta)           - 包含数据库配置和根页指针\n");
    printf("  Page 1: 元数据页 (Meta, 备份)     - 写时交替更新\n");
    printf("  Page 2: 分支页 (Branch)           - B+ 树的根节点\n");
    printf("  Page 3: 叶页 (Leaf)               - 实际数据: alpha, beta, gamma\n");
    printf("  Page 4: 叶页 (Leaf)               - 实际数据: delta, epsilon\n");
}

/**
 * 内存映射视图
 */
void demonstrate_mmap_view() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   内存映射视图                                             ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("LMDB 使用 mmap 将整个数据库文件映射到进程地址空间：\n");
    printf("\n");

    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│  虚拟地址空间                                            │\n");
    printf("│                                                         │\n");
    printf("│  me_map ─────► ┌────────────────────────────────┐      │\n");
    printf("│               │  Page 0: Meta                  │      │\n");
    printf("│               ├────────────────────────────────┤      │\n");
    printf("│               │  Page 1: Meta (备份)           │      │\n");
    printf("│               ├────────────────────────────────┤      │\n");
    printf("│               │  Page 2: Branch (根)          │      │\n");
    printf("│               ├────────────────────────────────┤      │\n");
    printf("│               │  Page 3: Leaf                 │      │\n");
    printf("│               ├────────────────────────────────┤      │\n");
    printf("│               │  Page 4: Leaf                 │      │\n");
    printf("│               ├────────────────────────────────┤      │\n");
    printf("│               │  ...                           │      │\n");
    printf("│               └────────────────────────────────┘      │\n");
    printf("│                                                         │\n");
    printf("│  访问第 N 页: me_map + (N * PAGE_SIZE)                   │\n");
    printf("└─────────────────────────────────────────────────────────┘\n");
    printf("\n");

    printf(COLOR_YELLOW "优势：" COLOR_RESET "\n");
    printf("  1. 零拷贝读取 - 数据直接从映射返回\n");
    printf("  2. 操作系统管理缓存 - 自动换页\n");
    printf("  3. 简化代码 - 不需要手动缓冲\n");
    printf("  4. 延迟分配 - 只在需要时使用物理内存\n");
}

int main() {
    printf(COLOR_CYAN "\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     LMDB 底层数据结构深度解析                              ║\n");
    printf("║     Internal Data Structures Deep Dive                    ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf(COLOR_RESET);

    printf("\n这个演示程序将带你深入了解 LMDB 的核心数据结构。\n");
    printf("这些结构在 lmdb.h 中是 opaque 的，但理解它们对于\n");
    printf("掌握 LMDB 的工作原理至关重要。\n");

    demonstrate_env_layout();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 继续..." COLOR_RESET);
    getchar();

    demonstrate_txn_layout();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 继续..." COLOR_RESET);
    getchar();

    demonstrate_page_layout();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 继续..." COLOR_RESET);
    getchar();

    demonstrate_node_layout();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 继续..." COLOR_RESET);
    getchar();

    demonstrate_cursor_layout();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 继续..." COLOR_RESET);
    getchar();

    demonstrate_reader_table();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 继续..." COLOR_RESET);
    getchar();

    demonstrate_mmap_view();

    printf(COLOR_CYAN "\n" COLOR_BOLD "按 Enter 查看实际数据库..." COLOR_RESET);
    getchar();

    examine_real_database();

    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   总结                                                    ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf(COLOR_YELLOW "核心结构关系：" COLOR_RESET "\n");
    printf("  MDB_env 包含整个数据库的内存映射\n");
    printf("  MDB_txn 代表一个事务，引用 MDB_env\n");
    printf("  MDB_cursor 遍历 MDB_txn 中的 B+ 树\n");
    printf("  MDB_page 是 B+ 树的节点，存储键值对\n");
    printf("  MDB_node 是页面内的条目\n");

    printf("\n" COLOR_YELLOW "内存访问路径：" COLOR_RESET "\n");
    printf("  1. API 调用 → mdb_get()\n");
    printf("  2. 事务 → txn->mt_dbs[dbi]\n");
    printf("  3. 根页 → mt_dbs[dbi].md_root\n");
    printf("  4. B+ 树遍历 → 通过页面的节点数组\n");
    printf("  5. 返回数据 → 直接指向内存映射中的值\n");

    printf("\n" COLOR_GREEN "✓ internals_demo 完成!\n" COLOR_RESET);
    printf("  数据库创建在: ./internals_demo_db/\n");
    printf("  使用 mdb_stat 查看更多信息\n");

    return 0;
}
