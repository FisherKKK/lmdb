/**
 * bulk_loader.c - High-Performance Bulk Data Import/Export Tool
 *
 * This utility provides efficient bulk import/export capabilities for LMDB.
 * It demonstrates:
 * - Batch insertion optimization
 * - Transaction sizing strategies
 * - Progress tracking for large datasets
 * - Memory-efficient streaming
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <lmdb.h>

#define PRINT_ERROR(msg) fprintf(stderr, "Error: %s\n", msg)

/* Colors for output */
#define COLOR_RESET   "\033[0m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

/* Default batch size */
#define DEFAULT_BATCH_SIZE 10000

/**
 * Get current time in milliseconds
 */
double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

/**
 * Format number with commas
 */
void format_number(char* buf, size_t len, size_t num) {
    if (num < 1000) {
        snprintf(buf, len, "%zu", num);
    } else if (num < 1000000) {
        snprintf(buf, len, "%zu,%03zu", num / 1000, num % 1000);
    } else {
        snprintf(buf, len, "%zu,%03zu,%03zu",
                 num / 1000000, (num / 1000) % 1000, num % 1000);
    }
}

/**
 * Import data from text file
 * Format: key value (one per line, tab or space separated)
 */
int import_text_file(const char* db_path, const char* input_file,
                     size_t batch_size) {
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;
    FILE* fp;
    char line[1024];
    size_t total_imported = 0;
    size_t batch_count = 0;
    double start_time = get_time_ms();

    printf(COLOR_CYAN "Importing from %s to %s" COLOR_RESET "\n", input_file, db_path);
    printf("Batch size: %zu\n\n", batch_size);

    /* Open environment */
    int rc = mdb_env_create(&env);
    if (rc != 0) {
        fprintf(stderr, "Failed to create env: %s\n", mdb_strerror(rc));
        return -1;
    }

    mdb_env_set_mapsize(env, 1024 * 1024 * 1024);  /* 1GB */
    mdb_env_set_maxdbs(env, 1);

    rc = mdb_env_open(env, db_path, 0, 0664);
    if (rc != 0) {
        fprintf(stderr, "Failed to open env: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return -1;
    }

    /* Open input file */
    fp = fopen(input_file, "r");
    if (!fp) {
        fprintf(stderr, "Failed to open input file: %s\n", input_file);
        mdb_env_close(env);
        return -1;
    }

    /* Begin first transaction */
    mdb_txn_begin(env, NULL, 0, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);

    printf("Progress:\n");

    /* Read and import line by line */
    while (fgets(line, sizeof(line), fp)) {
        /* Parse key and value */
        char* key = strtok(line, " \t\n");
        char* value = strtok(NULL, "\n");

        if (!key || !value) continue;

        MDB_val k = { .mv_size = strlen(key), .mv_data = key };
        MDB_val v = { .mv_size = strlen(value), .mv_data = value };

        rc = mdb_put(txn, dbi, &k, &v, 0);
        if (rc != 0) {
            fprintf(stderr, "Failed to put: %s\n", mdb_strerror(rc));
            break;
        }

        total_imported++;
        batch_count++;

        /* Commit batch */
        if (batch_count >= batch_size) {
            mdb_txn_commit(txn);

            /* Print progress */
            char num_buf[32];
            format_number(num_buf, sizeof(num_buf), total_imported);
            double elapsed = get_time_ms() - start_time;
            printf("  Imported %s records (%.0f records/sec)\r",
                   num_buf, (total_imported / elapsed) * 1000);
            fflush(stdout);

            /* Start new transaction */
            mdb_txn_begin(env, NULL, 0, &txn);
            batch_count = 0;
        }
    }

    /* Commit final batch */
    if (batch_count > 0) {
        mdb_txn_commit(txn);
    }

    fclose(fp);
    mdb_dbi_close(env, dbi);
    mdb_env_close(env);

    double elapsed = get_time_ms() - start_time;
    char num_buf[32];
    format_number(num_buf, sizeof(num_buf), total_imported);

    printf("\n\n" COLOR_GREEN "Import complete!" COLOR_RESET "\n");
    printf("  Total records: %s\n", num_buf);
    printf("  Time: %.2f seconds\n", elapsed / 1000.0);
    printf("  Rate: %.0f records/sec\n", (total_imported / elapsed) * 1000);

    return 0;
}

/**
 * Export database to text file
 */
int export_to_text_file(const char* db_path, const char* output_file) {
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_cursor *cursor;
    MDB_val key, data;
    FILE* fp;
    size_t total_exported = 0;
    double start_time = get_time_ms();

    printf(COLOR_CYAN "Exporting from %s to %s" COLOR_RESET "\n", db_path, output_file);

    /* Open environment */
    int rc = mdb_env_create(&env);
    if (rc != 0) {
        fprintf(stderr, "Failed to create env: %s\n", mdb_strerror(rc));
        return -1;
    }

    rc = mdb_env_open(env, db_path, MDB_RDONLY, 0664);
    if (rc != 0) {
        fprintf(stderr, "Failed to open env: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return -1;
    }

    /* Begin read-only transaction */
    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);

    /* Open output file */
    fp = fopen(output_file, "w");
    if (!fp) {
        fprintf(stderr, "Failed to open output file: %s\n", output_file);
        mdb_txn_abort(txn);
        mdb_dbi_close(env, dbi);
        mdb_env_close(env);
        return -1;
    }

    /* Open cursor */
    mdb_cursor_open(txn, dbi, &cursor);

    /* Iterate and export */
    while (mdb_cursor_get(cursor, &key, &data, MDB_NEXT) == 0) {
        fprintf(fp, "%.*s\t%.*s\n",
                (int)key.mv_size, (char*)key.mv_data,
                (int)data.mv_size, (char*)data.mv_data);
        total_exported++;

        if (total_exported % 10000 == 0) {
            char num_buf[32];
            format_number(num_buf, sizeof(num_buf), total_exported);
            printf("  Exported %s records\r", num_buf);
            fflush(stdout);
        }
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
    fclose(fp);

    double elapsed = get_time_ms() - start_time;
    char num_buf[32];
    format_number(num_buf, sizeof(num_buf), total_exported);

    printf("\n\n" COLOR_GREEN "Export complete!" COLOR_RESET "\n");
    printf("  Total records: %s\n", num_buf);
    printf("  Time: %.2f seconds\n", elapsed / 1000.0);
    printf("  Rate: %.0f records/sec\n", (total_exported / elapsed) * 1000);

    return 0;
}

/**
 * Generate sample data for testing
 */
int generate_sample_data(const char* output_file, size_t num_records) {
    FILE* fp = fopen(output_file, "w");
    if (!fp) {
        fprintf(stderr, "Failed to create output file\n");
        return -1;
    }

    printf(COLOR_CYAN "Generating %zu sample records..." COLOR_RESET "\n", num_records);

    for (size_t i = 0; i < num_records; i++) {
        char key[64], value[64];
        snprintf(key, sizeof(key), "key%010zu", i);
        snprintf(value, sizeof(value), "value%zu", i);

        fprintf(fp, "%s\t%s\n", key, value);

        if ((i + 1) % 10000 == 0) {
            printf("  Generated %zu records\r", i + 1);
            fflush(stdout);
        }
    }

    fclose(fp);
    printf("\n" COLOR_GREEN "Done! Output: %s" COLOR_RESET "\n", output_file);

    return 0;
}

/**
 * Benchmark different batch sizes
 */
void benchmark_batch_sizes(size_t total_records) {
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;

    size_t batch_sizes[] = {100, 500, 1000, 5000, 10000, 50000};
    int num_sizes = sizeof(batch_sizes) / sizeof(batch_sizes[0]);

    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║         BATCH SIZE BENCHMARK                                   ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");
    printf("Total records: %zu\n\n", total_records);

    printf("┌────────────┬─────────────┬──────────────┬──────────────┐\n");
    printf("│ Batch Size │ Time (sec)  │ Records/sec  │ Commits      │\n");
    printf("├────────────┼─────────────┼──────────────┼──────────────┤\n");

    for (int i = 0; i < num_sizes; i++) {
        size_t batch_size = batch_sizes[i];

        /* Create environment */
        mdb_env_create(&env);
        mdb_env_set_mapsize(env, 1024 * 1024 * 1024);
        char db_path[64];
        snprintf(db_path, sizeof(db_path), "./bench_db_%zu", batch_size);
        mdb_env_open(env, db_path, 0, 0664);

        double start = get_time_ms();

        mdb_txn_begin(env, NULL, 0, &txn);
        mdb_dbi_open(txn, NULL, 0, &dbi);

        size_t batch_count = 0;
        size_t num_commits = 0;

        for (size_t j = 0; j < total_records; j++) {
            char key[32], value[32];
            snprintf(key, sizeof(key), "key%010zu", j);
            snprintf(value, sizeof(value), "value%zu", j);

            MDB_val k = { .mv_size = strlen(key), .mv_data = key };
            MDB_val v = { .mv_size = strlen(value), .mv_data = value };
            mdb_put(txn, dbi, &k, &v, 0);

            batch_count++;
            if (batch_count >= batch_size) {
                mdb_txn_commit(txn);
                num_commits++;
                mdb_txn_begin(env, NULL, 0, &txn);
                batch_count = 0;
            }
        }

        if (batch_count > 0) {
            mdb_txn_commit(txn);
            num_commits++;
        }

        double elapsed = (get_time_ms() - start) / 1000.0;
        double rec_per_sec = total_records / elapsed;

        printf("│ %10zu │ %11.2f │ %12.0f │ %12zu │\n",
               batch_size, elapsed, rec_per_sec, num_commits);

        mdb_dbi_close(env, dbi);
        mdb_env_close(env);

        /* Cleanup */
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "rm -rf %s", db_path);
        system(cmd);
    }

    printf("└────────────┴─────────────┴──────────────┴──────────────┘\n");

    printf("\n" COLOR_YELLOW "Observations:" COLOR_RESET "\n");
    printf("  • Larger batches = fewer commits = faster\n");
    printf("  • Too large = more memory = diminishing returns\n");
    printf("  • Optimal batch size: 5,000 - 10,000 records\n");
}

void print_usage(const char* prog) {
    printf("Usage: %s <command> [options]\n", prog);
    printf("\nCommands:\n");
    printf("  import <db_path> <input_file> [batch_size]\n");
    printf("      Import data from text file to database\n");
    printf("      Default batch size: %d\n", DEFAULT_BATCH_SIZE);
    printf("\n");
    printf("  export <db_path> <output_file>\n");
    printf("      Export database to text file\n");
    printf("\n");
    printf("  generate <output_file> <num_records>\n");
    printf("      Generate sample data file\n");
    printf("\n");
    printf("  benchmark <num_records>\n");
    printf("      Benchmark different batch sizes\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s import ./mydb ./data.txt 5000\n", prog);
    printf("  %s export ./mydb ./output.txt\n", prog);
    printf("  %s generate ./sample.txt 100000\n", prog);
    printf("  %s benchmark 50000\n", prog);
}

int main(int argc, char** argv) {
    printf(COLOR_CYAN "\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     LMDB Bulk Loader - High-Performance Import/Export      ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf(COLOR_RESET);

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char* command = argv[1];

    if (strcmp(command, "import") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s import <db_path> <input_file> [batch_size]\n", argv[0]);
            return 1;
        }

        const char* db_path = argv[2];
        const char* input_file = argv[3];
        size_t batch_size = argc > 4 ? atol(argv[4]) : DEFAULT_BATCH_SIZE;

        return import_text_file(db_path, input_file, batch_size);

    } else if (strcmp(command, "export") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s export <db_path> <output_file>\n", argv[0]);
            return 1;
        }

        return export_to_text_file(argv[2], argv[3]);

    } else if (strcmp(command, "generate") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s generate <output_file> <num_records>\n", argv[0]);
            return 1;
        }

        return generate_sample_data(argv[2], atol(argv[3]));

    } else if (strcmp(command, "benchmark") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s benchmark <num_records>\n", argv[0]);
            return 1;
        }

        benchmark_batch_sizes(atol(argv[2]));
        return 0;

    } else {
        fprintf(stderr, "Unknown command: %s\n", command);
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}
