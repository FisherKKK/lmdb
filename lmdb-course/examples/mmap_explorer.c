/**
 * mmap_explorer.c - Day 2: Memory-Mapped I/O Exploration Tool
 *
 * This program explores how LMDB uses memory-mapped files and demonstrates
 * the difference between default mode and WRITEMAP mode.
 *
 * Learning Objectives:
 * - Understand how memory mapping works
 * - Visualize page faults and memory access patterns
 * - Compare default mode vs WRITEMAP mode
 * - Measure the performance impact of different mmap strategies
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <lmdb.h>

#define PRINT_ERROR(msg) fprintf(stderr, "Error: %s\n", msg)

/* Colors for output */
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"

/* Page size typically 4096 bytes */
#define PAGE_SIZE 4096

/**
 * Demonstrate basic memory mapping concepts
 */
void demonstrate_basic_mmap() {
    printf(COLOR_CYAN "\n=== Basic Memory Mapping Demo ===\n" COLOR_RESET);

    const char* filename = "mmap_test_file.dat";
    const size_t file_size = 1024 * 1024; /* 1 MB */

    /* Create a test file */
    int fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        PRINT_ERROR("Failed to create test file");
        return;
    }

    /* Extend file to desired size */
    if (ftruncate(fd, file_size) == -1) {
        PRINT_ERROR("Failed to set file size");
        close(fd);
        return;
    }

    printf("Created file: %s (size: %zu bytes)\n", filename, file_size);

    /* Memory map the file */
    char* mapped = mmap(NULL, file_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        PRINT_ERROR("Failed to mmap file");
        close(fd);
        return;
    }

    printf(COLOR_GREEN "File mapped at address: %p\n" COLOR_RESET, (void*)mapped);

    /* Demonstrate that we can write to memory as if it's an array */
    printf("\nWriting to mapped memory...\n");
    for (size_t i = 0; i < 100; i++) {
        mapped[i] = 'A' + (i % 26);
    }

    /* Read it back */
    printf("First 100 bytes: ");
    for (size_t i = 0; i < 100; i++) {
        putchar(mapped[i]);
    }
    printf("\n");

    /* Clean up */
    munmap(mapped, file_size);
    close(fd);
    unlink(filename);

    printf(COLOR_GREEN "Basic mmap demo completed!\n" COLOR_RESET);
}

/**
 * Measure page fault behavior
 */
void measure_page_faults() {
    printf(COLOR_CYAN "\n=== Page Fault Measurement ===\n" COLOR_RESET);

    const char* filename = "pagefault_test.dat";
    const size_t file_size = 10 * 1024 * 1024; /* 10 MB */
    const size_t num_pages = file_size / PAGE_SIZE;

    int fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        PRINT_ERROR("Failed to create test file");
        return;
    }

    if (ftruncate(fd, file_size) == -1) {
        PRINT_ERROR("Failed to set file size");
        close(fd);
        return;
    }

    char* mapped = mmap(NULL, file_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        PRINT_ERROR("Failed to mmap file");
        close(fd);
        return;
    }

    printf("Mapped %zu pages (%zu MB)\n", num_pages, file_size / (1024 * 1024));

    /* Access pages sequentially and measure time */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (size_t i = 0; i < num_pages; i++) {
        /* Touch first byte of each page to trigger page fault */
        mapped[i * PAGE_SIZE] = (char)i;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    long elapsed_ns = (end.tv_sec - start.tv_sec) * 1000000000L +
                      (end.tv_nsec - start.tv_nsec);
    double elapsed_ms = elapsed_ns / 1000000.0;

    printf("Sequential access: %.2f ms\n", elapsed_ms);
    printf("Average per page: %.2f µs\n", (elapsed_ms * 1000) / num_pages);

    /* Random access pattern */
    srand((unsigned int)time(NULL));
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (size_t i = 0; i < num_pages; i++) {
        size_t page = (size_t)((double)rand() / RAND_MAX * num_pages);
        mapped[page * PAGE_SIZE] = (char)page;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    elapsed_ns = (end.tv_sec - start.tv_sec) * 1000000000L +
                 (end.tv_nsec - start.tv_nsec);
    elapsed_ms = elapsed_ns / 1000000.0;

    printf("Random access:     %.2f ms\n", elapsed_ms);
    printf("Average per page:  %.2f µs\n", (elapsed_ms * 1000) / num_pages);

    printf(COLOR_YELLOW "\nObservation:\n" COLOR_RESET);
    printf("First access to each page triggers a page fault (disk I/O)\n");
    printf("Subsequent accesses are served from memory (much faster)\n");

    munmap(mapped, file_size);
    close(fd);
    unlink(filename);
}

/**
 * Compare LMDB default mode vs WRITEMAP mode
 */
void compare_lmdb_modes() {
    printf(COLOR_CYAN "\n=== LMDB Mode Comparison ===\n" COLOR_RESET);

    const int num_ops = 10000;
    const int value_size = 100;

    /* Test default mode */
    {
        printf("\n" COLOR_YELLOW "Testing DEFAULT mode..." COLOR_RESET "\n");

        MDB_env *env;
        MDB_txn *txn;
        MDB_dbi dbi;

        mdb_env_create(&env);
        mdb_env_set_mapsize(env, 1024 * 1024 * 100);
        mdb_env_set_maxdbs(env, 2);

        /* Open without WRITEMAP flag */
        if (mdb_env_open(env, "./testdb_default", 0, 0664) != 0) {
            PRINT_ERROR("Failed to open environment (default mode)");
            mdb_env_close(env);
            goto test_writemap;
        }

        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        mdb_txn_begin(env, NULL, 0, &txn);
        mdb_dbi_open(txn, NULL, 0, &dbi);

        char value[value_size];
        memset(value, 'X', value_size);

        for (int i = 0; i < num_ops; i++) {
            MDB_val key = { .mv_size = sizeof(int), .mv_data = &i };
            MDB_val data = { .mv_size = value_size, .mv_data = value };
            mdb_put(txn, dbi, &key, &data, 0);
        }

        mdb_txn_commit(txn);

        clock_gettime(CLOCK_MONOTONIC, &end);
        long elapsed_ns = (end.tv_sec - start.tv_sec) * 1000000000L +
                          (end.tv_nsec - start.tv_nsec);
        double elapsed_ms = elapsed_ns / 1000000.0;

        printf("  Time: %.2f ms\n", elapsed_ms);
        printf("  Ops/sec: %.0f\n", (num_ops * 1000.0) / elapsed_ms);

        mdb_dbi_close(env, dbi);
        mdb_env_close(env);
    }

test_writemap:
    /* Test WRITEMAP mode */
    {
        printf("\n" COLOR_YELLOW "Testing WRITEMAP mode..." COLOR_RESET "\n");

        MDB_env *env;
        MDB_txn *txn;
        MDB_dbi dbi;

        mdb_env_create(&env);
        mdb_env_set_mapsize(env, 1024 * 1024 * 100);
        mdb_env_set_maxdbs(env, 2);

        /* Open WITH WRITEMAP flag */
        if (mdb_env_open(env, "./testdb_writemap", MDB_WRITEMAP, 0664) != 0) {
            PRINT_ERROR("Failed to open environment (writemap mode)");
            mdb_env_close(env);
            goto cleanup;
        }

        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        mdb_txn_begin(env, NULL, 0, &txn);
        mdb_dbi_open(txn, NULL, 0, &dbi);

        char value[value_size];
        memset(value, 'X', value_size);

        for (int i = 0; i < num_ops; i++) {
            MDB_val key = { .mv_size = sizeof(int), .mv_data = &i };
            MDB_val data = { .mv_size = value_size, .mv_data = value };
            mdb_put(txn, dbi, &key, &data, 0);
        }

        mdb_txn_commit(txn);

        clock_gettime(CLOCK_MONOTONIC, &end);
        long elapsed_ns = (end.tv_sec - start.tv_sec) * 1000000000L +
                          (end.tv_nsec - start.tv_nsec);
        double elapsed_ms = elapsed_ns / 1000000.0;

        printf("  Time: %.2f ms\n", elapsed_ms);
        printf("  Ops/sec: %.0f\n", (num_ops * 1000.0) / elapsed_ms);

        mdb_dbi_close(env, dbi);
        mdb_env_close(env);
    }

cleanup:
    printf(COLOR_YELLOW "\nKey Differences:\n" COLOR_RESET);
    printf("  " COLOR_GREEN "DEFAULT mode:" COLOR_RESET "\n");
    printf("    - Writes use a copy-on-write mechanism\n");
    printf("    - More memory copies, but safer\n");
    printf("    - Slightly slower for write-heavy workloads\n");
    printf("\n  " COLOR_GREEN "WRITEMAP mode:" COLOR_RESET "\n");
    printf("    - Direct write to memory map\n");
    printf("    - Fewer memory copies, faster\n");
    printf("    - Risk of stray pointer writes corrupting database\n");
    printf("    - Best for read-only or trusted environments\n");
}

/**
 * Demonstrate memory map sharing between processes
 */
void demonstrate_mmap_sharing() {
    printf(COLOR_CYAN "\n=== Memory Map Sharing Demo ===\n" COLOR_RESET);

    printf("This demonstrates how two processes can share memory through mmap.\n");
    printf("In LMDB, this is how multiple readers see the same data without copying.\n");

    const char* filename = "shared_mmap.dat";
    const size_t file_size = 1024;

    int fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        PRINT_ERROR("Failed to create shared file");
        return;
    }

    ftruncate(fd, file_size);

    /* Parent maps the file and writes data */
    char* mapped = mmap(NULL, file_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        PRINT_ERROR("Failed to mmap shared file");
        close(fd);
        return;
    }

    /* Write data */
    strcpy(mapped, "Hello from parent!");
    printf("Parent wrote: \"%s\"\n", mapped);

    /* In a real scenario, we would fork here. For demonstration,
     * we just show that the mapping persists. */
    printf("\nIn LMDB, this mechanism allows:\n");
    printf("  " COLOR_GREEN "•" COLOR_RESET " Multiple readers to access the same data\n");
    printf("  " COLOR_GREEN "•" COLOR_RESET " Zero data copying between processes\n");
    printf("  " COLOR_GREEN "•" COLOR_RESET " OS handles page caching automatically\n");
    printf("  " COLOR_GREEN "•" COLOR_RESET " Memory consistency across readers\n");

    munmap(mapped, file_size);
    close(fd);
    unlink(filename);
}

/**
 * Print memory map information
 */
void print_mmap_info() {
    printf(COLOR_CYAN "\n=== Memory Map Information ===\n" COLOR_RESET);

    printf("System page size: %d bytes\n", PAGE_SIZE);
    printf("\n" COLOR_YELLOW "Memory Mapping Benefits:" COLOR_RESET "\n");
    printf("  1. Zero-copy reads - data accessed directly from file cache\n");
    printf("  2. Simplified code - no manual read/write calls needed\n");
    printf("  3. Automatic paging - OS manages what's in memory\n");
    printf("  4. Cache coherence - multiple readers see same data\n");
    printf("  5. Lazy allocation - memory allocated on access\n");

    printf("\n" COLOR_YELLOW "LMDB-Specific Behavior:" COLOR_RESET "\n");
    printf("  • Entire database file is memory mapped\n");
    printf("  • Reads return pointers directly into the map\n");
    printf("  • Writes use copy-on-write (COW) for versioning\n");
    printf("  • MVCC implemented through multiple page versions\n");
    printf("  • OS handles flushing dirty pages to disk\n");
}

int main() {
    printf(COLOR_MAGENTA "\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     LMDB Memory-Mapped I/O Exploration Tool                ║\n");
    printf("║                    Day 2 - Advanced Examples               ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf(COLOR_RESET);

    print_mmap_info();
    demonstrate_basic_mmap();
    measure_page_faults();
    compare_lmdb_modes();
    demonstrate_mmap_sharing();

    printf(COLOR_CYAN "\n=== Summary ===\n" COLOR_RESET);
    printf("Memory mapping is the foundation of LMDB's performance.\n");
    printf("Understanding how mmap works helps you:\n");
    printf("  " COLOR_GREEN "•" COLOR_RESET " Choose appropriate mode (default vs WRITEMAP)\n");
    printf("  " COLOR_GREEN "•" COLOR_RESET " Optimize access patterns for cache efficiency\n");
    printf("  " COLOR_GREEN "•" COLOR_RESET " Understand page fault behavior\n");
    printf("  " COLOR_GREEN "•" COLOR_RESET " Debug performance issues\n");

    printf(COLOR_GREEN "\n✓ mmap_explorer completed!\n" COLOR_RESET);

    return 0;
}
