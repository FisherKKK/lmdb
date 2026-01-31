/**
 * page_viewer.c - Day 4: LMDB Page Structure Visualization Tool
 *
 * This program visualizes the internal page structure of an LMDB database.
 * It demonstrates how pages are organized and used to store data.
 *
 * Learning Objectives:
 * - Understand LMDB page layout (header, branches, leaves)
 * - Visualize page types (branch, leaf, meta, overflow)
 * - See how data is stored within pages
 * - Understand space utilization
 *
 * Note: This is a simplified demonstration. Actual LMDB page structures
 * are more complex and defined in mdb.c with specific bit packing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <lmdb.h>
#include <stdint.h>

#define PRINT_ERROR(msg) fprintf(stderr, "Error: %s\n", msg)

/* Colors for output */
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

/* LMDB Page Types (simplified for demonstration) */
typedef enum {
    PAGE_BRANCH = 2,   /* Branch page (internal B+tree node) */
    PAGE_LEAF   = 4,   /* Leaf page (contains actual data) */
    PAGE_OVERFLOW = 5, /* Overflow page (large data) */
    PAGE_META   = 6    /* Meta page (database metadata) */
} page_type_t;

/* Simplified page header structure */
typedef struct {
    uint16_t mp_pgno;      /* Page number */
    uint16_t mp_flags;     /* Page flags (type) */
    uint16_t mp_lower;     /* Lower bound of free space */
    uint16_t mp_upper;     /* Upper bound of free space */
    uint32_t mp_pages;     /* Number of overflow pages */
} page_header_t;

/* Node structure within a page */
typedef struct {
    uint16_t ksize;        /* Key size */
    uint16_t vsize;        /* Value size */
    uint32_t pgno;         /* Page number (for branch nodes) */
    uint32_t dummy;        /* Alignment */
} node_t;

/**
 * Get page type name as string
 */
const char* page_type_name(uint16_t flags) {
    switch (flags & 0xFF) {
        case PAGE_BRANCH:  return "BRANCH";
        case PAGE_LEAF:    return "LEAF";
        case PAGE_OVERFLOW: return "OVERFLOW";
        case PAGE_META:    return "META";
        default:           return "UNKNOWN";
    }
}

/**
 * Print a page header visualization
 */
void visualize_page_header(const char* label, size_t page_num) {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║  Page #%zu %-40s ║\n", page_num, label);
    printf("╚══════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
}

/**
 * Visualize a branch page (internal B+tree node)
 */
void visualize_branch_page(size_t page_num, int num_keys) {
    visualize_page_header("BRANCH PAGE (Internal Node)", page_num);

    printf(COLOR_YELLOW "Page Type:" COLOR_RESET " BRANCH (Internal B+tree node)\n");
    printf(COLOR_YELLOW "Purpose:" COLOR_RESET "  Contains pointers to child pages\n");
    printf("\n");

    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│                     PAGE HEADER                          │\n");
    printf("├─────────────────────────────────────────────────────────┤\n");
    printf("│ Page Number:  %5zu                                       │\n", page_num);
    printf("│ Page Type:    BRANCH                                    │\n");
    printf("│ Num Keys:     %5d                                       │\n", num_keys);
    printf("│ Lower Bound:  0x%04x  (start of free space)            │\n", 64 + num_keys * 16);
    printf("│ Upper Bound:  0x%04x  (end of free space)              │\n", 4096 - num_keys * 12);
    printf("└─────────────────────────────────────────────────────────┘\n");
    printf("\n");

    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│                     PAGE CONTENTS                        │\n");
    printf("├─────────────────────────────────────────────────────────┤\n");

    for (int i = 0; i < num_keys; i++) {
        printf("│ [%2d] Key: ", i);
        printf("%-12s ", i < 5 ? "alpha" : "omega");
        printf("→ Page: %-6zu                          │\n", (size_t)(page_num * 2 + i));
    }

    printf("└─────────────────────────────────────────────────────────┘\n");
    printf("\n");
    printf(COLOR_GREEN "Structure:" COLOR_RESET "\n");
    printf("  Branch pages contain separators (keys) and page pointers\n");
    printf("  They guide searches through the B+tree\n");
    printf("  No actual data values - just navigation structure\n");
}

/**
 * Visualize a leaf page (contains actual data)
 */
void visualize_leaf_page(size_t page_num, int num_keys) {
    visualize_page_header("LEAF PAGE (Data Node)", page_num);

    printf(COLOR_YELLOW "Page Type:" COLOR_RESET " LEAF (Contains actual key-value pairs)\n");
    printf(COLOR_YELLOW "Purpose:" COLOR_RESET "  Stores the actual database records\n");
    printf("\n");

    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│                     PAGE HEADER                          │\n");
    printf("├─────────────────────────────────────────────────────────┤\n");
    printf("│ Page Number:  %5zu                                       │\n", page_num);
    printf("│ Page Type:    LEAF                                      │\n");
    printf("│ Num Keys:     %5d                                       │\n", num_keys);
    printf("│ Flags:        0x0000                                    │\n");
    printf("│ Lower Bound:  0x%04x                                    │\n", 64 + num_keys * 20);
    printf("│ Upper Bound:  0x%04x                                    │\n", 4096 - num_keys * 30);
    printf("└─────────────────────────────────────────────────────────┘\n");
    printf("\n");

    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│                     PAGE CONTENTS                        │\n");
    printf("├─────────────────────────────────────────────────────────┤\n");

    for (int i = 0; i < num_keys && i < 6; i++) {
        char key[32], value[32];
        snprintf(key, sizeof(key), "key%05d", i * 100);
        snprintf(value, sizeof(value), "value%05d", i * 100);

        printf("│ [%2d] Key: %-12s  Value: %-20s    │\n", i, key, value);
    }

    if (num_keys > 6) {
        printf("│ ...                                                  │\n");
    }

    printf("└─────────────────────────────────────────────────────────┘\n");
    printf("\n");
    printf(COLOR_GREEN "Structure:" COLOR_RESET "\n");
    printf("  Leaf pages contain actual key-value pairs\n");
    printf("  This is where the database data is stored\n");
    printf("  Sequential scan is efficient (sorted by key)\n");
}

/**
 * Visualize an overflow page (large data)
 */
void visualize_overflow_page(size_t page_num, size_t total_pages, size_t data_size) {
    visualize_page_header("OVERFLOW PAGE", page_num);

    printf(COLOR_YELLOW "Page Type:" COLOR_RESET " OVERFLOW (Large value storage)\n");
    printf(COLOR_YELLOW "Purpose:" COLOR_RESET "  Stores values too large for leaf page\n");
    printf("\n");

    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│                     PAGE HEADER                          │\n");
    printf("├─────────────────────────────────────────────────────────┤\n");
    printf("│ Page Number:  %5zu                                       │\n", page_num);
    printf("│ Page Type:    OVERFLOW                                  │\n");
    printf("│ Total Pages:  %5zu  (multi-page value)                  │\n", total_pages);
    printf("│ Data Size:    %5zu bytes                                │\n", data_size);
    printf("└─────────────────────────────────────────────────────────┘\n");
    printf("\n");

    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│                     PAGE CONTENTS                        │\n");
    printf("├─────────────────────────────────────────────────────────┤\n");
    printf("│  [Large data continues here...]                         │\n");
    printf("│  [This is page %zu of %zu in sequence]                   │\n", page_num, total_pages);
    printf("│                                                         │\n");
    printf("│  Typical use cases:                                     │\n");
    printf("│    • Large blobs (images, documents)                    │\n");
    printf("│    • Long text fields                                   │\n");
    printf("│    • Binary data                                        │\n");
    printf("└─────────────────────────────────────────────────────────┘\n");
    printf("\n");
    printf(COLOR_GREEN "Structure:" COLOR_RESET "\n");
    printf("  Overflow pages form linked lists for large values\n");
    printf("  Leaf page stores pointer to first overflow page\n");
    printf("  Each page links to the next in the sequence\n");
}

/**
 * Visualize a meta page (database metadata)
 */
void visualize_meta_page(size_t page_num) {
    visualize_page_header("META PAGE", page_num);

    printf(COLOR_YELLOW "Page Type:" COLOR_RESET " META (Database metadata)\n");
    printf(COLOR_YELLOW "Purpose:" COLOR_RESET "  Stores database configuration and state\n");
    printf("\n");

    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│                     PAGE HEADER                          │\n");
    printf("├─────────────────────────────────────────────────────────┤\n");
    printf("│ Page Number:  %5zu  (Meta pages are at 0 and 1)         │\n", page_num);
    printf("│ Page Type:    META                                      │\n");
    printf("│ Magic:        0xBEEFC0DE  (LMDB signature)              │\n");
    printf("│ Version:      1                                          │\n");
    printf("└─────────────────────────────────────────────────────────┘\n");
    printf("\n");

    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│                     META CONTENTS                        │\n");
    printf("├─────────────────────────────────────────────────────────┤\n");
    printf("│ Map Size:       104857600 bytes (100 MB)                │\n");
    printf("│ Last Txn ID:    42                                         │\n");
    printf("│ Last Page No:   153                                        │\n");
    printf("│ Max Readers:    126                                        │\n");
    printf("│ Num Readers:    3                                          │\n");
    printf("│ Trust Meta:     Yes                                        │\n");
    printf("└─────────────────────────────────────────────────────────┘\n");
    printf("\n");
    printf(COLOR_GREEN "Structure:" COLOR_RESET "\n");
    printf("  Two meta pages exist (page 0 and page 1)\n");
    printf("  They alternate for durability (write-ahead style)\n");
    printf("  Contains root page numbers for all databases\n");
}

/**
 * Visualize page free space
 */
void visualize_free_space(int used_percent) {
    printf("\n┌─ FREE SPACE VISIZATION ─────────────────────────────────┐\n");
    printf("│                                                         │\n");

    int bar_width = 54;
    int filled = (bar_width * used_percent) / 100;

    printf("│  Used: ");
    printf(COLOR_GREEN);
    for (int i = 0; i < filled; i++) printf("█");
    printf(COLOR_RESET);
    for (int i = filled; i < bar_width; i++) printf("░");
    printf(" %3d%%  │\n", used_percent);

    printf("│                                                         │\n");
    printf("│  " COLOR_CYAN "Upper bound" COLOR_RESET " ← Free space → " COLOR_CYAN "Lower bound" COLOR_RESET "        │\n");
    printf("│  (data grows from bottom)    (indices grow from top)   │\n");
    printf("└─────────────────────────────────────────────────────────┘\n");
}

/**
 * Show B+tree structure
 */
void visualize_btree_structure() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║              SAMPLE B+TREE STRUCTURE                       ║\n");
    printf("╚══════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("                  " COLOR_YELLOW "[ROOT - Branch Page 1]" COLOR_RESET "\n");
    printf("                  Keys: apple, cherry, pear\n");
    printf("                    │    │     │    │\n");
    printf("         ┌──────────┘    │     │    └──────────┐\n");
    printf("         ▼               ▼     ▼               ▼\n");
    printf("   " COLOR_CYAN "[Branch Page 2]" COLOR_RESET " " COLOR_CYAN "[Leaf Page 3]" COLOR_RESET " " COLOR_CYAN "[Leaf Page 4]" COLOR_RESET " " COLOR_CYAN "[Branch Page 5]" COLOR_RESET "\n");
    printf("   Keys: apricot...     Keys:      Keys:      Keys: orange...\n");
    printf("   (A-B range)          cherry      pear       (N-Z range)\n");
    printf("    │                                                        │\n");
    printf("    └──────────┬────────────┬────────────┬────────────┘    │\n");
    printf("               ▼            ▼            ▼                   │\n");
    printf("         " COLOR_GREEN "[Leaf 6]" COLOR_RESET " " COLOR_GREEN "[Leaf 7]" COLOR_RESET " " COLOR_GREEN "[Leaf 8]" COLOR_RESET " " COLOR_GREEN "[Leaf 9]" COLOR_RESET "\n");
    printf("         apricot       banana       grape        orange\n");
    printf("         avocado       berry        kiwi         peach\n");
    printf("\n");
    printf(COLOR_YELLOW "Key Points:" COLOR_RESET "\n");
    printf("  • All leaf pages are at the same level (balanced tree)\n");
    printf("  • Data is only in leaf pages (not branch pages)\n");
    printf("  • Branch pages guide navigation through the tree\n");
    printf("  • Range scans are efficient (follow leaf links)\n");
}

/**
 * Create sample database and show its pages
 */
void create_sample_database() {
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;

    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║         CREATING SAMPLE DATABASE                                ║\n");
    printf("╚══════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    /* Create environment */
    if (mdb_env_create(&env) != 0) {
        PRINT_ERROR("Failed to create environment");
        return;
    }

    mdb_env_set_mapsize(env, 1024 * 1024 * 100);
    mdb_env_set_maxdbs(env, 1);

    if (mdb_env_open(env, "./pageview_db", 0, 0664) != 0) {
        PRINT_ERROR("Failed to open environment");
        mdb_env_close(env);
        return;
    }

    /* Write transaction */
    mdb_txn_begin(env, NULL, 0, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);

    /* Insert some sample data */
    for (int i = 0; i < 100; i++) {
        char key[32], value[32];
        snprintf(key, sizeof(key), "key%05d", i);
        snprintf(value, sizeof(value), "value%05d_data_here", i);

        MDB_val k = { .mv_size = strlen(key), .mv_data = key };
        MDB_val v = { .mv_size = strlen(value), .mv_data = value };
        mdb_put(txn, dbi, &k, &v, 0);
    }

    /* Add one large value to demonstrate overflow */
    char large_key[] = "large_value_key";
    char* large_value = malloc(10000);
    memset(large_value, 'X', 10000 - 1);
    large_value[10000 - 1] = '\0';

    MDB_val lk = { .mv_size = strlen(large_key), .mv_data = large_key };
    MDB_val lv = { .mv_size = 10000, .mv_data = large_value };
    mdb_put(txn, dbi, &lk, &lv, 0);
    free(large_value);

    mdb_txn_commit(txn);

    /* Read transaction to show stats */
    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);

    MDB_stat stat;
    mdb_stat(txn, dbi, &stat);

    printf("\n" COLOR_GREEN "Database Statistics:" COLOR_RESET "\n");
    printf("  Page size:     %u bytes\n", stat.ms_psize);
    printf("  Depth:         %u levels\n", stat.ms_depth);
    printf("  Branch pages:  %zu\n", stat.ms_branch_pages);
    printf("  Leaf pages:    %zu\n", stat.ms_leaf_pages);
    printf("  Overflow pages:%zu\n", stat.ms_overflow_pages);
    printf("  Entries:       %zu\n", stat.ms_entries);

    mdb_txn_abort(txn);
    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}

int main(int argc, char** argv) {
    printf(COLOR_MAGENTA "\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     LMDB Page Structure Visualization Tool                ║\n");
    printf("║                    Day 4 - Page Layout                    ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf(COLOR_RESET);

    printf("\nThis tool demonstrates how LMDB organizes data in pages.\n");
    printf("Pages are 4096 bytes (default) and contain headers and data.\n");

    /* Show meta pages first */
    visualize_meta_page(0);
    visualize_meta_page(1);

    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter to continue..." COLOR_RESET);
    getchar();

    /* Show branch pages */
    visualize_branch_page(2, 4);
    visualize_free_space(45);

    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter to continue..." COLOR_RESET);
    getchar();

    /* Show leaf pages */
    visualize_leaf_page(10, 15);
    visualize_free_space(75);

    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter to continue..." COLOR_RESET);
    getchar();

    /* Show overflow pages */
    visualize_overflow_page(100, 3, 10000);

    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter to create sample database..." COLOR_RESET);
    getchar();

    /* Create real sample database */
    create_sample_database();

    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter to see B+tree structure..." COLOR_RESET);
    getchar();

    /* Show B+tree structure */
    visualize_btree_structure();

    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║                    SUMMARY                                    ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    printf("\n" COLOR_YELLOW "LMDB Page Organization:" COLOR_RESET "\n");
    printf("  1. " COLOR_GREEN "Meta pages" COLOR_RESET " (pages 0,1) - Database metadata\n");
    printf("  2. " COLOR_GREEN "Branch pages" COLOR_RESET " - Internal B+tree nodes (navigation)\n");
    printf("  3. " COLOR_GREEN "Leaf pages" COLOR_RESET " - Actual key-value data\n");
    printf("  4. " COLOR_GREEN "Overflow pages" COLOR_RESET " - Large values spanning pages\n");

    printf("\n" COLOR_YELLOW "Page Layout:" COLOR_RESET "\n");
    printf("  • Header at top (page number, flags, bounds)\n");
    printf("  • Keys grow from top down\n");
    printf("  • Data grows from bottom up\n");
    printf("  • Free space in the middle\n");

    printf("\n" COLOR_YELLOW "Key Insights:" COLOR_RESET "\n");
    printf("  • Page size is fixed (default 4KB)\n");
    printf("  • Efficient for keys < 2KB (fits in one page)\n");
    printf("  • Large values use overflow pages\n");
    printf("  • Tree is kept balanced for consistent performance\n");

    printf(COLOR_GREEN "\n✓ page_viewer completed!\n" COLOR_RESET);
    printf("  Database created at: ./pageview_db/\n");
    printf("  Use mdb_stat to see actual page layout.\n");

    return 0;
}
