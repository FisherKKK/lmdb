/**
 * mvcc_demo.c - Day 8: Multi-Version Concurrency Control (MVCC) Demo
 *
 * This program demonstrates how LMDB's MVCC system enables concurrent
 * access without blocking between readers and writers.
 *
 * Learning Objectives:
 * - Understand MVCC version management
 * - See how readers don't block writers
 * - Visualize version chains
 * - Understand reader slot management
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
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

/* Thread data */
typedef struct {
    MDB_env* env;
    int thread_id;
    int delay_ms;
    int is_writer;
} thread_data_t;

/**
 * Visualize MVCC concept
 */
void visualize_mvcc_concept() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║         MVCC: MULTI-VERSION CONCURRENCY CONTROL               ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("MVCC allows multiple versions of the same data to coexist:\n");
    printf("\n");

    printf("Timeline example:\n");
    printf("\n");
    printf("  Time 10: " COLOR_GREEN "Writer 1" COLOR_RESET " writes 'apple'  → Version 1\n");
    printf("  Time 20: " COLOR_GREEN "Reader A" COLOR_RESET " starts (sees Version 1)\n");
    printf("  Time 30: " COLOR_GREEN "Writer 2" COLOR_RESET " writes 'APPLE'  → Version 2\n");
    printf("  Time 40: " COLOR_GREEN "Reader B" COLOR_RESET " starts (sees Version 2)\n");
    printf("  Time 50: " COLOR_GREEN "Reader A" COLOR_RESET " still sees Version 1!\n");
    printf("  Time 60: " COLOR_GREEN "Reader A" COLOR_RESET " finishes\n");
    printf("  Time 70: Version 1 can be freed\n");
    printf("\n");

    printf(COLOR_YELLOW "Key Point: " COLOR_RESET "Reader A never waited for Writer 2!\n");
    printf("Both readers and writers operate concurrently.\n");
    printf("\n");

    printf("LMDB MVCC Properties:\n");
    printf("  • " COLOR_GREEN "Readers never block writers" COLOR_RESET "\n");
    printf("  • " COLOR_GREEN "Writers never block readers" COLOR_RESET "\n");
    printf("  • " COLOR_GREEN "Only one writer at a time" COLOR_RESET " (they block each other)\n");
    printf("  • " COLOR_GREEN "Readers see consistent snapshot" COLOR_RESET " (as of transaction start)\n");
}

/**
 * Visualize version chain
 */
void visualize_version_chain() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║         VERSION CHAIN VISUALIZATION                          ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("Key 'user:1' version history:\n");
    printf("\n");

    printf("  Current: " COLOR_GREEN "[Page 50] value='John Doe'" COLOR_RESET "\n");
    printf("           │\n");
    printf("           ▼\n");
    printf("  Old:     " COLOR_YELLOW "[Page 12] value='John'" COLOR_RESET "  (Reader A using)\n");
    printf("           │\n");
    printf("           ▼\n");
    printf("  Older:   " COLOR_RED "[Page 5] value='J. Doe'" COLOR_RESET "  (can be freed)\n");
    printf("\n");

    printf("How it works:\n");
    printf("  1. Each modification creates new page (COW)\n");
    printf("  2. Old pages remain for active readers\n");
    printf("  3. Pages freed when no readers need them\n");
    printf("  4. B+tree updated to point to newest version\n");
    printf("\n");

    printf("Page lifecycle:\n");
    printf("  " COLOR_GREEN "NEW" COLOR_RESET " → " COLOR_CYAN "CURRENT" COLOR_RESET " → " COLOR_YELLOW "OLD (readers)" COLOR_RESET " → " COLOR_RED "FREED" COLOR_RESET "\n");
}

/**
 * Visualize reader table
 */
void visualize_reader_table() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║         READER TABLE MANAGEMENT                                ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("Reader table (in lock file):\n");
    printf("\n");

    printf("┌─────────┬──────────┬─────────────┬──────────┐\n");
    printf("│ Slot    │ PID      │ Thread ID   │ Txn ID   │\n");
    printf("├─────────┼──────────┼─────────────┼──────────┤\n");
    printf("│ 0       │ 1234     │ 1001        │ 50       │ " COLOR_GREEN "(active)" COLOR_RESET "\n");
    printf("│ 1       │ 1235     │ 2001        │ 55       │ " COLOR_GREEN "(active)" COLOR_RESET "\n");
    printf("│ 2       │ 1234     │ 1002        │ 60       │ " COLOR_GREEN "(active)" COLOR_RESET "\n");
    printf("│ 3       │ 0        │ 0           │ 0        │ " COLOR_YELLOW "(free)" COLOR_RESET "\n");
    printf("│ 4       │ 0        │ 0           │ 0        │ " COLOR_YELLOW "(free)" COLOR_RESET "\n");
    printf("└─────────┴──────────┴─────────────┴──────────┘\n");
    printf("\n");

    printf("Writer checks reader table before freeing pages:\n");
    printf("  Writer txn ID: 65\n");
    printf("  Oldest reader txn ID: 50\n");
    printf("  → Can free pages from txn < 50\n");
    printf("  → Must keep pages from txn >= 50\n");
    printf("\n");

    printf("Stale reader detection:\n");
    printf("  • If PID no longer exists, slot is stale\n");
    printf("  • mdb_reader_check() clears stale slots\n");
    printf("  • Stale slots block page reuse until cleared\n");
}

/**
 * Reader thread function
 */
void* reader_thread(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;

    printf(COLOR_BLUE "[Reader %d] Starting..." COLOR_RESET "\n", data->thread_id);

    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data_val;

    /* Begin read-only transaction */
    int rc = mdb_txn_begin(data->env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) {
        fprintf(stderr, "Reader %d: Failed to begin txn: %s\n",
                data->thread_id, mdb_strerror(rc));
        return NULL;
    }

    size_t txn_id = mdb_txn_id(txn);
    printf(COLOR_BLUE "[Reader %d] Transaction ID: %zu" COLOR_RESET "\n",
           data->thread_id, txn_id);

    mdb_dbi_open(txn, NULL, 0, &dbi);

    /* Read some keys */
    for (int i = 0; i < 3; i++) {
        char key_str[32];
        snprintf(key_str, sizeof(key_str), "key%d", i);

        key.mv_data = key_str;
        key.mv_size = strlen(key_str);

        rc = mdb_get(txn, dbi, &key, &data_val);
        if (rc == 0) {
            printf(COLOR_BLUE "[Reader %d] Read: %s = %.*s" COLOR_RESET "\n",
                   data->thread_id, key_str,
                   (int)data_val.mv_size, (char*)data_val.mv_data);
        } else {
            printf(COLOR_BLUE "[Reader %d] Key '%s' not found" COLOR_RESET "\n",
                   data->thread_id, key_str);
        }
    }

    /* Sleep to simulate long-running reader */
    if (data->delay_ms > 0) {
        printf(COLOR_BLUE "[Reader %d] Sleeping %d ms..." COLOR_RESET "\n",
               data->thread_id, data->delay_ms);
        usleep(data->delay_ms * 1000);
    }

    printf(COLOR_BLUE "[Reader %d] Finishing (txn ID %zu)" COLOR_RESET "\n",
           data->thread_id, txn_id);

    mdb_dbi_close(data->env, dbi);
    mdb_txn_abort(txn);  /* Can abort for read-only */

    return NULL;
}

/**
 * Writer thread function
 */
void* writer_thread(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;

    printf(COLOR_GREEN "[Writer %d] Starting..." COLOR_RESET "\n", data->thread_id);

    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data_val;

    /* Retry if write transaction is blocked */
    int retries = 0;
    int rc;

    while (retries < 10) {
        rc = mdb_txn_begin(data->env, NULL, 0, &txn);
        if (rc == 0) {
            break;
        } else if (rc == MDB_TXN_FULL) {
            printf(COLOR_YELLOW "[Writer %d] Transaction full, retrying..." COLOR_RESET "\n",
                   data->thread_id);
            retries++;
            usleep(1000);  /* Wait 1ms */
        } else {
            fprintf(stderr, "Writer %d: Failed to begin txn: %s\n",
                    data->thread_id, mdb_strerror(rc));
            return NULL;
        }
    }

    if (rc != 0) {
        fprintf(stderr, "Writer %d: Gave up after %d retries\n",
                data->thread_id, retries);
        return NULL;
    }

    size_t txn_id = mdb_txn_id(txn);
    printf(COLOR_GREEN "[Writer %d] Transaction ID: %zu" COLOR_RESET "\n",
           data->thread_id, txn_id);

    mdb_dbi_open(txn, NULL, 0, &dbi);

    /* Write some data */
    for (int i = 0; i < 3; i++) {
        char key_str[32], value_str[32];
        snprintf(key_str, sizeof(key_str), "key%d", i + data->thread_id * 10);
        snprintf(value_str, sizeof(value_str), "value%d.%d",
                 data->thread_id, i);

        key.mv_data = key_str;
        key.mv_size = strlen(key_str);
        data_val.mv_data = value_str;
        data_val.mv_size = strlen(value_str);

        mdb_put(txn, dbi, &key, &data_val, 0);
        printf(COLOR_GREEN "[Writer %d] Wrote: %s = %s" COLOR_RESET "\n",
               data->thread_id, key_str, value_str);
    }

    rc = mdb_txn_commit(txn);
    if (rc != 0) {
        fprintf(stderr, "Writer %d: Failed to commit: %s\n",
                data->thread_id, mdb_strerror(rc));
    } else {
        printf(COLOR_GREEN "[Writer %d] Committed txn %zu" COLOR_RESET "\n",
               data->thread_id, txn_id);
    }

    mdb_dbi_close(data->env, dbi);

    return NULL;
}

/**
 * Demo 1: Concurrent readers
 */
void demo_concurrent_readers() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   Demo 1: Multiple Concurrent Readers                         ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;

    /* Setup */
    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024);
    mdb_env_set_maxreaders(env, 16);
    mdb_env_open(env, "./mvcc_demo1", 0, 0664);

    /* Add initial data */
    mdb_txn_begin(env, NULL, 0, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);
    for (int i = 0; i < 5; i++) {
        char key[32], value[32];
        snprintf(key, sizeof(key), "key%d", i);
        snprintf(value, sizeof(value), "initial_value%d", i);
        MDB_val k = { .mv_size = strlen(key), .mv_data = key };
        MDB_val v = { .mv_size = strlen(value), .mv_data = value };
        mdb_put(txn, dbi, &k, &v, 0);
    }
    mdb_txn_commit(txn);

    /* Create reader threads */
    pthread_t threads[3];
    thread_data_t data[3] = {
        {env, 1, 100, 0},
        {env, 2, 100, 0},
        {env, 3, 100, 0}
    };

    printf("\n" COLOR_YELLOW "Starting 3 concurrent readers..." COLOR_RESET "\n\n");

    for (int i = 0; i < 3; i++) {
        pthread_create(&threads[i], NULL, reader_thread, &data[i]);
    }

    for (int i = 0; i < 3; i++) {
        pthread_join(threads[i], NULL);
    }

    printf(COLOR_GREEN "\nAll readers completed without blocking each other!\n" COLOR_RESET);

    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}

/**
 * Demo 2: Reader and writer concurrent
 */
void demo_reader_writer() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   Demo 2: Concurrent Reader and Writer                      ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;

    /* Setup */
    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024);
    mdb_env_set_maxreaders(env, 16);
    mdb_env_open(env, "./mvcc_demo2", 0, 0664);

    /* Add initial data */
    mdb_txn_begin(env, NULL, 0, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);
    for (int i = 0; i < 5; i++) {
        char key[32], value[32];
        snprintf(key, sizeof(key), "key%d", i);
        snprintf(value, sizeof(value), "value%d", i);
        MDB_val k = { .mv_size = strlen(key), .mv_data = key };
        MDB_val v = { .mv_size = strlen(value), .mv_data = value };
        mdb_put(txn, dbi, &k, &v, 0);
    }
    mdb_txn_commit(txn);

    /* Create threads: reader, writer, reader */
    pthread_t threads[3];
    thread_data_t data[3] = {
        {env, 1, 300, 0},  /* Long-running reader */
        {env, 1, 0, 1},    /* Writer */
        {env, 2, 50, 0}    /* Quick reader */
    };

    printf("\n" COLOR_YELLOW "Starting: Reader (300ms) → Writer → Reader (50ms)" COLOR_RESET "\n");
    printf(COLOR_YELLOW "Note: Writer doesn't wait for first reader!" COLOR_RESET "\n\n");

    pthread_create(&threads[0], NULL, reader_thread, &data[0]);
    usleep(50000);  /* Start writer after 50ms */
    pthread_create(&threads[1], NULL, writer_thread, &data[1]);
    usleep(100000); /* Start second reader after 100ms */
    pthread_create(&threads[2], NULL, reader_thread, &data[2]);

    for (int i = 0; i < 3; i++) {
        pthread_join(threads[i], NULL);
    }

    printf(COLOR_GREEN "\nMVCC in action: Reader and Writer operated concurrently!\n" COLOR_RESET);

    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}

/**
 * Demo 3: Show consistent snapshot
 */
void demo_consistent_snapshot() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   Demo 3: Consistent Snapshot                                 ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    MDB_env *env;
    MDB_txn *txn_w, *txn_r1, *txn_r2;
    MDB_dbi dbi;
    MDB_val key, data;

    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024);
    mdb_env_open(env, "./mvcc_demo3", 0, 0664);

    /* Initial state */
    mdb_txn_begin(env, NULL, 0, &txn_w);
    mdb_dbi_open(txn_w, NULL, 0, &dbi);
    key.mv_data = "counter"; key.mv_size = 7;
    data.mv_data = "100"; data.mv_size = 3;
    mdb_put(txn_w, dbi, &key, &data, 0);
    mdb_txn_commit(txn_w);

    printf("\n" COLOR_YELLOW "Step 1: Reader 1 starts (sees counter=100)" COLOR_RESET "\n");
    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn_r1);
    mdb_dbi_open(txn_r1, NULL, 0, &dbi);
    key.mv_data = "counter"; key.mv_size = 7;
    mdb_get(txn_r1, dbi, &key, &data);
    printf("  Reader 1 sees: counter = %.*s\n", (int)data.mv_size, (char*)data.mv_data);

    printf(COLOR_YELLOW "\nStep 2: Writer updates counter to 200" COLOR_RESET "\n");
    mdb_txn_begin(env, NULL, 0, &txn_w);
    mdb_dbi_open(txn_w, NULL, 0, &dbi);
    key.mv_data = "counter"; key.mv_size = 7;
    data.mv_data = "200"; data.mv_size = 3;
    mdb_put(txn_w, dbi, &key, &data, 0);
    mdb_txn_commit(txn_w);
    printf("  Writer committed: counter = 200\n");

    printf(COLOR_YELLOW "\nStep 3: Reader 2 starts (sees counter=200)" COLOR_RESET "\n");
    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn_r2);
    mdb_dbi_open(txn_r2, NULL, 0, &dbi);
    key.mv_data = "counter"; key.mv_size = 7;
    mdb_get(txn_r2, dbi, &key, &data);
    printf("  Reader 2 sees: counter = %.*s\n", (int)data.mv_size, (char*)data.mv_data);

    printf(COLOR_YELLOW "\nStep 4: Reader 1 reads again" COLOR_RESET "\n");
    mdb_get(txn_r1, dbi, &key, &data);
    printf("  Reader 1 still sees: counter = %.*s " COLOR_GREEN "(consistent snapshot!)" COLOR_RESET "\n",
           (int)data.mv_size, (char*)data.mv_data);

    mdb_txn_abort(txn_r1);
    mdb_txn_abort(txn_r2);
    mdb_dbi_close(env, dbi);
    mdb_env_close(env);

    printf(COLOR_GREEN "\nEach reader sees a consistent snapshot from its transaction start!\n" COLOR_RESET);
}

int main() {
    printf(COLOR_CYAN "\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     LMDB MVCC Demonstration                                 ║\n");
    printf("║                    Day 8 - Version Management               ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf(COLOR_RESET);

    visualize_mvcc_concept();

    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter to see version chain..." COLOR_RESET);
    getchar();
    visualize_version_chain();

    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter to see reader table..." COLOR_RESET);
    getchar();
    visualize_reader_table();

    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter to see Demo 1..." COLOR_RESET);
    getchar();
    demo_concurrent_readers();

    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter to see Demo 2..." COLOR_RESET);
    getchar();
    demo_reader_writer();

    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter to see Demo 3..." COLOR_RESET);
    getchar();
    demo_consistent_snapshot();

    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║                    SUMMARY                                    ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    printf("\n" COLOR_YELLOW "MVCC Benefits:" COLOR_RESET "\n");
    printf("  1. " COLOR_GREEN "No blocking" COLOR_RESET " between readers and writers\n");
    printf("  2. " COLOR_GREEN "Consistent snapshots" COLOR_RESET " for each transaction\n");
    printf("  3. " COLOR_GREEN "High concurrency" COLOR_RESET " for read-heavy workloads\n");
    printf("  4. " COLOR_GREEN "Simple programming model" COLOR_RESET " (no locks needed)\n");

    printf("\n" COLOR_YELLOW "How it Works:" COLOR_RESET "\n");
    printf("  • Copy-on-write creates new versions\n");
    printf("  • Old versions kept for active readers\n");
    printf("  • Reader table tracks active transactions\n");
    printf("  • Pages freed when no longer needed\n");

    printf(COLOR_GREEN "\n✓ mvcc_demo completed!\n" COLOR_RESET);

    return 0;
}
