/**
 * db_compare.c - Database Comparison and Verification Tool
 *
 * This utility compares two LMDB databases and shows differences.
 * Useful for:
 * - Verifying database integrity
 * - Comparing backup copies
 * - Testing database replication
 * - Auditing database changes
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lmdb.h>

#define PRINT_ERROR(msg) fprintf(stderr, "Error: %s\n", msg)

/* Colors for output */
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

/* Comparison statistics */
typedef struct {
    size_t keys_only_in_a;
    size_t keys_only_in_b;
    size_t keys_different;
    size_t keys_same;
    size_t total_compared;
} compare_stats_t;

/**
 * Compare two databases
 */
int compare_databases(const char* path_a, const char* path_b,
                     compare_stats_t* stats, int verbose) {
    MDB_env *env_a, *env_b;
    MDB_txn *txn_a, *txn_b;
    MDB_dbi dbi_a, dbi_b;
    MDB_cursor *cursor_a, *cursor_b;
    MDB_val key_a, data_a, key_b, data_b;
    int rc;

    memset(stats, 0, sizeof(*stats));

    /* Open environment A */
    rc = mdb_env_create(&env_a);
    if (rc != 0) {
        fprintf(stderr, "Failed to create env A: %s\n", mdb_strerror(rc));
        return -1;
    }
    rc = mdb_env_open(env_a, path_a, MDB_RDONLY, 0664);
    if (rc != 0) {
        fprintf(stderr, "Failed to open env A: %s\n", mdb_strerror(rc));
        mdb_env_close(env_a);
        return -1;
    }

    /* Open environment B */
    rc = mdb_env_create(&env_b);
    if (rc != 0) {
        fprintf(stderr, "Failed to create env B: %s\n", mdb_strerror(rc));
        mdb_env_close(env_a);
        return -1;
    }
    rc = mdb_env_open(env_b, path_b, MDB_RDONLY, 0664);
    if (rc != 0) {
        fprintf(stderr, "Failed to open env B: %s\n", mdb_strerror(rc));
        mdb_env_close(env_a);
        mdb_env_close(env_b);
        return -1;
    }

    /* Begin transactions */
    mdb_txn_begin(env_a, NULL, MDB_RDONLY, &txn_a);
    mdb_txn_begin(env_b, NULL, MDB_RDONLY, &txn_b);

    /* Open databases */
    mdb_dbi_open(txn_a, NULL, 0, &dbi_a);
    mdb_dbi_open(txn_b, NULL, 0, &dbi_b);

    /* Open cursors */
    mdb_cursor_open(txn_a, dbi_a, &cursor_a);
    mdb_cursor_open(txn_b, dbi_b, &cursor_b);

    printf(COLOR_CYAN "Comparing databases:" COLOR_RESET "\n");
    printf("  A: %s\n", path_a);
    printf("  B: %s\n", path_b);
    printf("\n");

    /* Iterate through both databases in parallel */
    rc = mdb_cursor_get(cursor_a, &key_a, &data_a, MDB_FIRST);
    int rc_b = mdb_cursor_get(cursor_b, &key_b, &data_b, MDB_FIRST);

    int cmp_result = 0;

    while (rc == 0 || rc_b == 0) {
        if (rc == 0 && rc_b == 0) {
            /* Both have data, compare keys */
            cmp_result = mdb_cmp(txn_a, dbi_a, &key_a, &key_b);

            if (cmp_result == 0) {
                /* Same key, compare values */
                stats->total_compared++;

                int data_cmp = mdb_cmp(txn_a, dbi_a, &data_a, &data_b);
                if (data_cmp == 0) {
                    stats->keys_same++;
                    if (verbose) {
                        printf(COLOR_GREEN "  [=] Key: %.*s" COLOR_RESET "\n",
                               (int)key_a.mv_size, (char*)key_a.mv_data);
                    }
                } else {
                    stats->keys_different++;
                    printf(COLOR_YELLOW "  [!] Key: %.*s" COLOR_RESET "\n",
                           (int)key_a.mv_size, (char*)key_a.mv_data);
                    if (verbose) {
                        printf("      A: %.*s\n", (int)data_a.mv_size, (char*)data_a.mv_data);
                        printf("      B: %.*s\n", (int)data_b.mv_size, (char*)data_b.mv_data);
                    }
                }

                rc = mdb_cursor_get(cursor_a, &key_a, &data_a, MDB_NEXT);
                rc_b = mdb_cursor_get(cursor_b, &key_b, &data_b, MDB_NEXT);

            } else if (cmp_result < 0) {
                /* Key A < Key B: key only in A */
                stats->keys_only_in_a++;
                printf(COLOR_RED "  [-] Only in A: %.*s" COLOR_RESET "\n",
                       (int)key_a.mv_size, (char*)key_a.mv_data);
                rc = mdb_cursor_get(cursor_a, &key_a, &data_a, MDB_NEXT);
            } else {
                /* Key A > Key B: key only in B */
                stats->keys_only_in_b++;
                printf(COLOR_RED "  [+] Only in B: %.*s" COLOR_RESET "\n",
                       (int)key_b.mv_size, (char*)key_b.mv_data);
                rc_b = mdb_cursor_get(cursor_b, &key_b, &data_b, MDB_NEXT);
            }

        } else if (rc == 0) {
            /* Only A has data */
            stats->keys_only_in_a++;
            printf(COLOR_RED "  [-] Only in A: %.*s" COLOR_RESET "\n",
                   (int)key_a.mv_size, (char*)key_a.mv_data);
            rc = mdb_cursor_get(cursor_a, &key_a, &data_a, MDB_NEXT);

        } else {
            /* Only B has data */
            stats->keys_only_in_b++;
            printf(COLOR_RED "  [+] Only in B: %.*s" COLOR_RESET "\n",
                   (int)key_b.mv_size, (char*)key_b.mv_data);
            rc_b = mdb_cursor_get(cursor_b, &key_b, &data_b, MDB_NEXT);
        }
    }

    /* Cleanup */
    mdb_cursor_close(cursor_a);
    mdb_cursor_close(cursor_b);
    mdb_dbi_close(env_a, dbi_a);
    mdb_dbi_close(env_b, dbi_b);
    mdb_txn_abort(txn_a);
    mdb_txn_abort(txn_b);
    mdb_env_close(env_a);
    mdb_env_close(env_b);

    return 0;
}

/**
 * Print comparison statistics
 */
void print_stats(compare_stats_t* stats) {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║         COMPARISON STATISTICS                                 ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│  Keys only in A:     %6zu                             │\n", stats->keys_only_in_a);
    printf("│  Keys only in B:     %6zu                             │\n", stats->keys_only_in_b);
    printf("│  Keys with diff val: %6zu                             │\n", stats->keys_different);
    printf("│  Keys identical:     %6zu                             │\n", stats->keys_same);
    printf("├─────────────────────────────────────────────────────────┤\n");
    printf("│  Total compared:     %6zu                             │\n", stats->total_compared);
    printf("│  Total differences:  %6zu                             │\n",
           stats->keys_only_in_a + stats->keys_only_in_b + stats->keys_different);
    printf("└─────────────────────────────────────────────────────────┘\n");
    printf("\n");

    if (stats->keys_only_in_a == 0 && stats->keys_only_in_b == 0 &&
        stats->keys_different == 0) {
        printf(COLOR_GREEN "✓ Databases are IDENTICAL!\n" COLOR_RESET);
    } else {
        printf(COLOR_YELLOW "⚠ Databases are DIFFERENT!\n" COLOR_RESET);
    }
}

/**
 * Create sample databases for comparison
 */
void create_sample_databases() {
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;

    /* Create database A */
    printf(COLOR_CYAN "Creating sample database A..." COLOR_RESET "\n");
    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024);
    mdb_env_open(env, "./compare_db_a", 0, 0664);
    mdb_txn_begin(env, NULL, 0, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);

    /* Add data */
    const char* keys_a[] = {"apple", "banana", "cherry", "date", "elderberry"};
    const char* vals_a[] = {"red", "yellow", "red", "brown", "purple"};

    for (int i = 0; i < 5; i++) {
        MDB_val k = { .mv_size = strlen(keys_a[i]), .mv_data = (void*)keys_a[i] };
        MDB_val v = { .mv_size = strlen(vals_a[i]), .mv_data = (void*)vals_a[i] };
        mdb_put(txn, dbi, &k, &v, 0);
    }
    mdb_txn_commit(txn);
    mdb_dbi_close(env, dbi);
    mdb_env_close(env);

    /* Create database B */
    printf(COLOR_CYAN "Creating sample database B..." COLOR_RESET "\n");
    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024);
    mdb_env_open(env, "./compare_db_b", 0, 0664);
    mdb_txn_begin(env, NULL, 0, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);

    /* Add similar data with some differences */
    const char* keys_b[] = {"apple", "banana", "cherry", "fig", "grape"};
    const char* vals_b[] = {"red", "yellow", "purple", "green", "purple"};

    for (int i = 0; i < 5; i++) {
        MDB_val k = { .mv_size = strlen(keys_b[i]), .mv_data = (void*)keys_b[i] };
        MDB_val v = { .mv_size = strlen(vals_b[i]), .mv_data = (void*)vals_b[i] };
        mdb_put(txn, dbi, &k, &v, 0);
    }
    mdb_txn_commit(txn);
    mdb_dbi_close(env, dbi);
    mdb_env_close(env);

    printf(COLOR_GREEN "Sample databases created!\n" COLOR_RESET);
}

void print_usage(const char* prog) {
    printf("Usage: %s [options] <db_path_a> <db_path_b>\n", prog);
    printf("\nOptions:\n");
    printf("  -v, --verbose    Show detailed differences\n");
    printf("  -s, --sample     Create sample databases for testing\n");
    printf("  -h, --help       Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s ./db_a ./db_b\n", prog);
    printf("  %s -v ./db_a ./db_b\n", prog);
    printf("  %s --sample\n", prog);
}

int main(int argc, char** argv) {
    int verbose = 0;
    int create_sample = 0;
    const char* path_a = NULL;
    const char* path_b = NULL;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--sample") == 0) {
            create_sample = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (path_a == NULL) {
            path_a = argv[i];
        } else if (path_b == NULL) {
            path_b = argv[i];
        } else {
            fprintf(stderr, "Too many arguments\n");
            print_usage(argv[0]);
            return 1;
        }
    }

    printf(COLOR_CYAN "\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     LMDB Database Comparison Tool                          ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf(COLOR_RESET);

    if (create_sample) {
        create_sample_databases();
        printf("\n" COLOR_YELLOW "Comparing sample databases..." COLOR_RESET "\n\n");
        compare_stats_t stats;
        compare_databases("./compare_db_a", "./compare_db_b", &stats, verbose);
        print_stats(&stats);
        return 0;
    }

    if (path_a == NULL || path_b == NULL) {
        fprintf(stderr, "Error: Two database paths required\n\n");
        print_usage(argv[0]);
        return 1;
    }

    compare_stats_t stats;
    if (compare_databases(path_a, path_b, &stats, verbose) != 0) {
        return 1;
    }

    print_stats(&stats);

    return 0;
}
