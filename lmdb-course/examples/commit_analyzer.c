/**
 * commit_analyzer.c - Day 7: Transaction Commit Analysis Tool
 *
 * This program analyzes and visualizes what happens during a transaction
 * commit in LMDB, including the write-ahead logging and page management.
 *
 * Learning Objectives:
 * - Understand the commit process steps
 - Visualize dirty page tracking
 * - See how durability is ensured
 * - Understand commit performance factors
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <lmdb.h>

#define PRINT_ERROR(msg) fprintf(stderr, "Error: %s\n", msg)

/* Colors for output */
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

/* Page tracking */
typedef struct {
    size_t page_num;
    int is_dirty;
    int is_overflow;
    size_t num_keys;
} page_info_t;

/* Transaction commit info */
typedef struct {
    size_t txn_id;
    size_t num_dirty_pages;
    size_t num_new_pages;
    size_t bytes_written;
    double duration_ms;
    page_info_t pages[100];
} commit_info_t;

static commit_info_t g_commit = {0};

/**
 * Simulate the commit process steps
 */
void visualize_commit_process() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║         TRANSACTION COMMIT PROCESS                           ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("Step 1: " COLOR_GREEN "Validation" COLOR_RESET "\n");
    printf("        • Check transaction is valid\n");
    printf("        • Verify no constraint violations\n");
    printf("        • Ensure B+tree invariants maintained\n");
    printf("\n");

    printf("Step 2: " COLOR_GREEN "Page Allocation" COLOR_RESET "\n");
    printf("        • Allocate new pages for modifications\n");
    printf("        • Track dirty pages (modified pages)\n");
    printf("        • Update free list\n");
    printf("\n");

    printf("Step 3: " COLOR_GREEN "Write-Ahead" COLOR_RESET "\n");
    printf("        • Write dirty pages to memory map\n");
    printf("        • Ensure all modifications are in buffer\n");
    printf("        • Update meta page with new transaction ID\n");
    printf("\n");

    printf("Step 4: " COLOR_GREEN "Flush/Sync" COLOR_RESET "\n");
    printf("        • Flush buffers to disk (msync)\n");
    printf("        • Ensure durability (depends on flags)\n");
    printf("        • Update meta page alternately (0 or 1)\n");
    printf("\n");

    printf("Step 5: " COLOR_GREEN "Reader Table Update" COLOR_RESET "\n");
    printf("        • Update reader slot with new transaction ID\n");
    printf("        • Mark previous slots as stale if needed\n");
    printf("        • Allow new readers to see committed data\n");
    printf("\n");

    printf("Step 6: " COLOR_GREEN "Cleanup" COLOR_RESET "\n");
    printf("        • Free old pages (after all readers finish)\n");
    printf("        • Return to free list\n");
    printf("        • Transaction complete\n");
}

/**
 * Show copy-on-write in action
 */
void visualize_cow() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║         COPY-ON-WRITE VISUALIZATION                           ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("Before modification:\n");
    printf("  Page 10: [A, B, C, D]  " COLOR_GREEN "(original)" COLOR_RESET "\n");
    printf("\n");

    printf("Transaction begins, modifies 'C' to 'X':\n");
    printf("  1. Allocate new page (Page 25)\n");
    printf("  2. Copy Page 10 to Page 25\n");
    printf("  3. Modify Page 25: [A, B, " COLOR_YELLOW "X" COLOR_RESET ", D]\n");
    printf("  4. Update B+tree to point to Page 25\n");
    printf("\n");

    printf("State during transaction:\n");
    printf("  Page 10: [A, B, C, D]  " COLOR_GREEN "(old, readers still see this)" COLOR_RESET "\n");
    printf("  Page 25: [A, B, X, D]  " COLOR_YELLOW "(new, writer sees this)" COLOR_RESET "\n");
    printf("\n");

    printf(COLOR_YELLOW "Key Point: " COLOR_RESET "Old page (10) stays intact for existing readers!\n");
    printf("New readers get Page 25 after commit.\n");
    printf("\n");

    printf("After commit (when all old readers finish):\n");
    printf("  Page 10: " COLOR_RED "[freed]" COLOR_RESET " → added to free list\n");
    printf("  Page 25: [A, B, X, D]  " COLOR_GREEN "(now the current version)" COLOR_RESET "\n");
}

/**
 * Show meta page updates
 */
void visualize_meta_pages() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║         META PAGE MANAGEMENT                                   ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("LMDB maintains two meta pages (page 0 and page 1):\n");
    printf("\n");

    printf("Initial state:\n");
    printf("  Page 0: " COLOR_GREEN "[VALID]" COLOR_RESET "  TxnID: 100\n");
    printf("  Page 1: [stale]  TxnID: 99\n");
    printf("\n");

    printf("Commit transaction 101:\n");
    printf("  1. Write new state to Page 1\n");
    printf("  2. Flush to disk\n");
    printf("  3. Page 1 is now valid\n");
    printf("\n");

    printf("After commit:\n");
    printf("  Page 0: [stale]  TxnID: 100\n");
    printf("  Page 1: " COLOR_GREEN "[VALID]" COLOR_RESET "  TxnID: 101\n");
    printf("\n");

    printf("Next commit (102):\n");
    printf("  Write to Page 0 (the other one)\n");
    printf("  Flip-flop pattern ensures recoverability\n");
    printf("\n");

    printf(COLOR_YELLOW "Why two meta pages?" COLOR_RESET "\n");
    printf("  • If crash during write, one meta page is still valid\n");
    printf("  • On recovery, use the meta page with higher TxnID\n");
    printf("  • Provides write-ahead logging semantics\n");
    printf("  • Ensures atomic commit\n");
}

/**
 * Demo: Measure commit performance
 */
void demo_commit_performance() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║         COMMIT PERFORMANCE ANALYSIS                           ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;

    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024 * 100);

    /* Test with different sync modes */
    const char* modes[] = {"No Sync", "Sync", "Map Async", "Map Async | No Meta Sync"};
    int flags[] = {MDB_NOSYNC, MDB_SYNC, 0, MDB_NOMETASYNC};

    for (int test = 0; test < 4; test++) {
        mdb_env_open(env, "./commit_perf_db", flags[test], 0664);

        struct timespec start, end;
        int num_commits = 100;

        clock_gettime(CLOCK_MONOTONIC, &start);

        for (int i = 0; i < num_commits; i++) {
            mdb_txn_begin(env, NULL, 0, &txn);
            mdb_dbi_open(txn, NULL, 0, &dbi);

            char key[32], value[32];
            snprintf(key, sizeof(key), "key%d", i);
            snprintf(value, sizeof(value), "value%d", i);

            MDB_val k = { .mv_size = strlen(key), .mv_data = key };
            MDB_val v = { .mv_size = strlen(value), .mv_data = value };
            mdb_put(txn, dbi, &k, &v, 0);

            mdb_txn_commit(txn);
        }

        clock_gettime(CLOCK_MONOTONIC, &end);

        long elapsed_ns = (end.tv_sec - start.tv_sec) * 1000000000L +
                          (end.tv_nsec - start.tv_nsec);
        double avg_ms = elapsed_ns / 1000000.0 / num_commits;

        printf("\nMode: " COLOR_YELLOW "%s" COLOR_RESET "\n", modes[test]);
        printf("  Total time: %.2f ms\n", elapsed_ns / 1000000.0);
        printf("  Avg per commit: %.3f ms\n", avg_ms);
        printf("  Commits/sec: %.0f\n", 1000.0 / avg_ms);

        mdb_env_close(env);

        /* Clean up for next test */
        system("rm -rf ./commit_perf_db");
        mdb_env_create(&env);
        mdb_env_set_mapsize(env, 1024 * 1024 * 100);
    }

    mdb_env_close(env);

    printf("\n" COLOR_YELLOW "Performance Notes:" COLOR_RESET "\n");
    printf("  • " COLOR_GREEN "No Sync" COLOR_RESET ": Fastest, risk of data loss on crash\n");
    printf("  • " COLOR_GREEN "Sync" COLOR_RESET ": Safest, slower (fsync for each commit)\n");
    printf("  • " COLOR_GREEN "Map Async" COLOR_RESET ": Balanced, lets OS manage flushes\n");
    printf("  • " COLOR_GREEN "No Meta Sync" COLOR_RESET ": Faster, slight risk\n");
}

/**
 * Demo: Batch commit optimization
 */
void demo_batch_optimization() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║         BATCH COMMIT OPTIMIZATION                            ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;

    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024 * 100);
    mdb_env_open(env, "./batch_demo", 0, 0664);

    /* Test 1: One commit per operation */
    struct timespec start, end;
    int num_ops = 1000;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < num_ops; i++) {
        mdb_txn_begin(env, NULL, 0, &txn);
        mdb_dbi_open(txn, NULL, 0, &dbi);

        char key[32], value[32];
        snprintf(key, sizeof(key), "key%d", i);
        snprintf(value, sizeof(value), "value%d", i);

        MDB_val k = { .mv_size = strlen(key), .mv_data = key };
        MDB_val v = { .mv_size = strlen(value), .mv_data = value };
        mdb_put(txn, dbi, &k, &v, 0);

        mdb_txn_commit(txn);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    long elapsed1_ns = (end.tv_sec - start.tv_sec) * 1000000000L +
                       (end.tv_nsec - start.tv_nsec);

    /* Test 2: Batch operations in one transaction */
    system("rm -rf ./batch_demo");
    mdb_env_open(env, "./batch_demo", 0, 0664);

    clock_gettime(CLOCK_MONOTONIC, &start);
    mdb_txn_begin(env, NULL, 0, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);

    for (int i = 0; i < num_ops; i++) {
        char key[32], value[32];
        snprintf(key, sizeof(key), "key%d", i);
        snprintf(value, sizeof(value), "value%d", i);

        MDB_val k = { .mv_size = strlen(key), .mv_data = key };
        MDB_val v = { .mv_size = strlen(value), .mv_data = value };
        mdb_put(txn, dbi, &k, &v, 0);
    }

    mdb_txn_commit(txn);
    clock_gettime(CLOCK_MONOTONIC, &end);

    long elapsed2_ns = (end.tv_sec - start.tv_sec) * 1000000000L +
                       (end.tv_nsec - start.tv_nsec);

    printf("\n" COLOR_YELLOW "Results for %d operations:" COLOR_RESET "\n", num_ops);
    printf("\nIndividual commits:\n");
    printf("  Time: %.2f ms\n", elapsed1_ns / 1000000.0);
    printf("  Ops/sec: %.0f\n", num_ops * 1000.0 / (elapsed1_ns / 1000000.0));
    printf("\nBatch commit (1 transaction):\n");
    printf("  Time: %.2f ms\n", elapsed2_ns / 1000000.0);
    printf("  Ops/sec: %.0f\n", num_ops * 1000.0 / (elapsed2_ns / 1000000.0));
    printf("\n" COLOR_GREEN "Speedup: %.1fx faster!\n" COLOR_RESET,
           (double)elapsed1_ns / elapsed2_ns);

    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}

/**
 * Visualize free list management
 */
void visualize_freelist() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║         FREE LIST MANAGEMENT                                  ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("During transaction commit:\n");
    printf("\n");
    printf("  1. Old pages (freed by this transaction):\n");
    printf("     → Added to free list for future reuse\n");
    printf("     → Not immediately available (readers may be using them)\n");
    printf("\n");

    printf("  2. New pages (allocated by this transaction):\n");
    printf("     → Taken from free list (if available)\n");
    printf("     → Or extend the database file\n");
    printf("\n");

    printf("Free list structure:\n");
    printf("  • Transaction IDs track when pages became free\n");
    printf("  • Pages can be reused when all older readers finish\n");
    printf("  • Prevents premature reuse (readers would see wrong data)\n");
    printf("\n");

    printf(COLOR_YELLOW "Example:" COLOR_RESET "\n");
    printf("  Free list entries: [(page 50, txn 100), (page 75, txn 105)]\n");
    printf("  Current transaction: 110\n");
    printf("  Oldest active reader: 102\n");
    printf("  → Page 50 can be reused (txn 100 < 102)\n");
    printf("  → Page 75 cannot be reused (txn 105 >= 102)\n");
}

/**
 * Visualize commit durability
 */
void visualize_durability() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║         DURABILITY GUARANTEES                                  ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("What " COLOR_GREEN "DURABLE" COLOR_RESET " means:\n");
    printf("  Once committed, data survives:\n");
    printf("  • Power loss\n");
    printf("  • System crash\n");
    printf("  • Application crash\n");
    printf("\n");

    printf("How LMDB ensures durability:\n");
    printf("\n");
    printf("  1. " COLOR_GREEN "Copy-on-Write" COLOR_RESET "\n");
    printf("     • Never modifies in-place\n");
    printf("     • Old versions intact until commit complete\n");
    printf("\n");

    printf("  2. " COLOR_GREEN "Flush to Disk" COLOR_RESET "\n");
    printf("     • msync() ensures data reaches storage\n");
    printf("     • Order matters: data before meta page\n");
    printf("\n");

    printf("  3. " COLOR_GREEN "Double Meta Pages" COLOR_RESET "\n");
    printf("     • Write-ahead logging\n");
    printf("     • One valid meta page at all times\n");
    printf("\n");

    printf("  4. " COLOR_GREEN "Checksums" COLOR_RESET "\n");
    printf("     • Detect corrupted pages\n");
    printf("     • Validate on open\n");
    printf("\n");

    printf(COLOR_YELLOW "Trade-offs:" COLOR_RESET "\n");
    printf("  • MDB_SYNC: Most durable, slower\n");
    printf("  • MDB_NOSYNC: Faster, risk of last transaction(s)\n");
    printf("  • MDB_NOMETASYNC: Good middle ground\n");
    printf("  • MDB_WRITEMAP: Faster, less safe (stray writes)\n");
}

int main() {
    printf(COLOR_CYAN "\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     LMDB Transaction Commit Analyzer                       ║\n");
    printf("║                    Day 7 - Commit Process                  ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf(COLOR_RESET);

    visualize_commit_process();

    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter to see copy-on-write..." COLOR_RESET);
    getchar();
    visualize_cow();

    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter to see meta page management..." COLOR_RESET);
    getchar();
    visualize_meta_pages();

    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter to see free list..." COLOR_RESET);
    getchar();
    visualize_freelist();

    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter to see durability..." COLOR_RESET);
    getchar();
    visualize_durability();

    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter to measure commit performance..." COLOR_RESET);
    getchar();
    demo_commit_performance();

    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter to see batch optimization..." COLOR_RESET);
    getchar();
    demo_batch_optimization();

    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║                    SUMMARY                                    ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    printf("\n" COLOR_YELLOW "Commit Process Steps:" COLOR_RESET "\n");
    printf("  1. Validate transaction\n");
    printf("  2. Allocate new pages (COW)\n");
    printf("  3. Write modifications to memory map\n");
    printf("  4. Flush dirty pages to disk\n");
    printf("  5. Update meta page\n");
    printf("  6. Update reader table\n");
    printf("\n");

    printf(COLOR_YELLOW "Performance Tips:" COLOR_RESET "\n");
    printf("  • Batch operations in single transaction\n");
    printf("  • Choose appropriate sync flags\n");
    printf("  • Keep transactions short\n");
    printf("  • Avoid long-running write transactions\n");

    printf(COLOR_GREEN "\n✓ commit_analyzer completed!\n" COLOR_RESET);

    return 0;
}
