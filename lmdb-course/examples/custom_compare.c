/**
 * custom_compare.c - Day 5: Custom Comparison Function Demo
 *
 * This program demonstrates how to use custom comparison functions in LMDB.
 * Custom comparators allow you to control the sort order of your database.
 *
 * Learning Objectives:
 * - Understand how LMDB compares keys
 * - Create custom comparison functions
 * - Use different ordering schemes (numeric, reverse, case-insensitive)
 * - Handle complex key types (structures, multi-field keys)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <lmdb.h>

#define PRINT_ERROR(msg) fprintf(stderr, "Error: %s\n", msg)

/* Colors for output */
#define COLOR_RESET   "\033[0m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

/**
 * Custom comparator 1: Numeric comparison
 * Compares integer keys stored as strings
 */
int compare_numeric(const MDB_val *a, const MDB_val *b) {
    /* Parse integers from strings */
    int ia = atoi((char*)a->mv_data);
    int ib = atoi((char*)b->mv_data);

    if (ia < ib) return -1;
    if (ia > ib) return 1;
    return 0;
}

/**
 * Custom comparator 2: Reverse string comparison
 * Sorts in descending order (Z to A)
 */
int compare_reverse(const MDB_val *a, const MDB_val *b) {
    size_t min_len = a->mv_size < b->mv_size ? a->mv_size : b->mv_size;
    int rc = memcmp(a->mv_data, b->mv_data, min_len);

    if (rc == 0) {
        if (a->mv_size < b->mv_size) return 1;
        if (a->mv_size > b->mv_size) return -1;
        return 0;
    }

    /* Reverse the comparison result */
    return rc > 0 ? -1 : 1;
}

/**
 * Custom comparator 3: Case-insensitive comparison
 */
int compare_case_insensitive(const MDB_val *a, const MDB_val *b) {
    size_t min_len = a->mv_size < b->mv_size ? a->mv_size : b->mv_size;

    for (size_t i = 0; i < min_len; i++) {
        char ca = tolower(((char*)a->mv_data)[i]);
        char cb = tolower(((char*)b->mv_data)[i]);

        if (ca < cb) return -1;
        if (ca > cb) return 1;
    }

    if (a->mv_size < b->mv_size) return -1;
    if (a->mv_size > b->mv_size) return 1;
    return 0;
}

/**
 * Custom comparator 4: Length-first comparison
 * Sorts by string length first, then alphabetically
 */
int compare_length_first(const MDB_val *a, const MDB_val *b) {
    if (a->mv_size < b->mv_size) return -1;
    if (a->mv_size > b->mv_size) return 1;

    /* Same length, compare lexicographically */
    return memcmp(a->mv_data, b->mv_data, a->mv_size);
}

/**
 * Custom comparator 5: Structured key comparison
 * Demonstrates multi-field keys (e.g., "timestamp:userid")
 */
int compare_timestamp_userid(const MDB_val *a, const MDB_val *b) {
    /* Keys are in format "timestamp:userid" */
    char* a_str = (char*)a->mv_data;
    char* b_str = (char*)b->mv_data;

    /* Parse timestamp (first field) */
    long ta = atol(a_str);
    long tb = atol(b_str);

    if (ta < tb) return -1;
    if (ta > tb) return 1;

    /* Same timestamp, compare user ID */
    char* a_colon = strchr(a_str, ':');
    char* b_colon = strchr(b_str, ':');

    if (a_colon && b_colon) {
        return strcmp(a_colon + 1, b_colon + 1);
    }

    return 0;
}

/**
 * Open database with custom comparator
 */
int open_custom_db(MDB_env* env, const char* path,
                    MDB_cmp_func* cmp, MDB_dbi* dbi) {
    MDB_txn *txn;
    int rc;

    /* Begin transaction */
    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc != 0) {
        fprintf(stderr, "Failed to begin transaction: %s\n", mdb_strerror(rc));
        return rc;
    }

    /* Open database with custom comparator */
    rc = mdb_dbi_open(txn, NULL, MDB_CREATE, dbi);
    if (rc != 0) {
        fprintf(stderr, "Failed to open database: %s\n", mdb_strerror(rc));
        mdb_txn_abort(txn);
        return rc;
    }

    /* Set custom comparator */
    rc = mdb_set_compare(txn, *dbi, cmp);
    if (rc != 0) {
        fprintf(stderr, "Failed to set comparator: %s\n", mdb_strerror(rc));
        mdb_txn_abort(txn);
        return rc;
    }

    /* Commit transaction */
    rc = mdb_txn_commit(txn);
    if (rc != 0) {
        fprintf(stderr, "Failed to commit: %s\n", mdb_strerror(rc));
        return rc;
    }

    return 0;
}

/**
 * Insert test data
 */
void insert_data(MDB_env* env, MDB_dbi dbi, const char** keys,
                 const char** values, int count) {
    MDB_txn *txn;

    mdb_txn_begin(env, NULL, 0, &txn);

    for (int i = 0; i < count; i++) {
        MDB_val key = { .mv_size = strlen(keys[i]), .mv_data = (void*)keys[i] };
        MDB_val val = { .mv_size = strlen(values[i]), .mv_data = (void*)values[i] };

        int rc = mdb_put(txn, dbi, &key, &val, 0);
        if (rc != 0) {
            fprintf(stderr, "Failed to insert %s: %s\n", keys[i], mdb_strerror(rc));
        }
    }

    mdb_txn_commit(txn);
}

/**
 * Display database contents
 */
void display_db(MDB_env* env, MDB_dbi dbi, const char* label) {
    MDB_txn *txn;
    MDB_cursor *cursor;
    MDB_val key, data;
    int count = 0;

    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    mdb_cursor_open(txn, dbi, &cursor);

    printf(COLOR_CYAN "\n%s:" COLOR_RESET "\n", label);

    while (mdb_cursor_get(cursor, &key, &data, MDB_NEXT) == 0) {
        printf("  [%2d] Key: %-20s → Value: %s\n", ++count,
               (char*)key.mv_data, (char*)data.mv_data);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
}

/**
 * Demo 1: Numeric ordering
 */
void demo_numeric() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   Demo 1: Numeric String Ordering                             ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    MDB_env *env;
    MDB_dbi dbi;

    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024);
    mdb_env_open(env, "./custom_numeric", 0, 0664);

    open_custom_db(env, "./custom_numeric", compare_numeric, &dbi);

    /* Insert numeric strings (would sort wrong with default lex compare) */
    const char* keys[] = {"100", "20", "3", "1000", "50", "1", "200", "25"};
    const char* values[] = {"hundred", "twenty", "three", "thousand",
                            "fifty", "one", "two-hundred", "twenty-five"};

    insert_data(env, dbi, keys, values, 8);
    display_db(env, dbi, "Numeric Order (1, 3, 20, 25, 50, 100, 200, 1000)");

    printf(COLOR_YELLOW "\nNotice: Numbers are sorted numerically, not lexicographically!\n");
    printf("Default lex order would be: 1, 100, 1000, 20, 200, 25, 3, 50\n" COLOR_RESET);

    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}

/**
 * Demo 2: Reverse ordering
 */
void demo_reverse() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   Demo 2: Reverse Ordering (Descending)                        ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    MDB_env *env;
    MDB_dbi dbi;

    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024);
    mdb_env_open(env, "./custom_reverse", 0, 0664);

    open_custom_db(env, "./custom_reverse", compare_reverse, &dbi);

    const char* keys[] = {"apple", "banana", "cherry", "date", "elderberry"};
    const char* values[] = {"A", "B", "C", "D", "E"};

    insert_data(env, dbi, keys, values, 5);
    display_db(env, dbi, "Reverse Order (Z to A)");

    printf(COLOR_YELLOW "\nNotice: Keys are sorted in descending order!\n" COLOR_RESET);

    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}

/**
 * Demo 3: Case-insensitive ordering
 */
void demo_case_insensitive() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   Demo 3: Case-Insensitive Ordering                           ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    MDB_env *env;
    MDB_dbi dbi;

    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024);
    mdb_env_open(env, "./custom_nocase", 0, 0664);

    open_custom_db(env, "./custom_nocase", compare_case_insensitive, &dbi);

    const char* keys[] = {"Apple", "banana", "CHERRY", "Date", "ELDERBERRY",
                          "fig", "Grape"};
    const char* values[] = {"1", "2", "3", "4", "5", "6", "7"};

    insert_data(env, dbi, keys, values, 7);
    display_db(env, dbi, "Case-Insensitive Order");

    printf(COLOR_YELLOW "\nNotice: Case is ignored for ordering!\n");
    printf("Apple, banana, CHERRY are treated as if they had the same case.\n" COLOR_RESET);

    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}

/**
 * Demo 4: Length-first ordering
 */
void demo_length_first() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   Demo 4: Length-First Ordering                               ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    MDB_env *env;
    MDB_dbi dbi;

    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024);
    mdb_env_open(env, "./custom_length", 0, 0664);

    open_custom_db(env, "./custom_length", compare_length_first, &dbi);

    const char* keys[] = {"a", "bb", "ccc", "dddd", "eeeee",
                          "aa", "bbb", "cccc", "ddddd"};
    const char* values[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};

    insert_data(env, dbi, keys, values, 9);
    display_db(env, dbi, "Length-First Order");

    printf(COLOR_YELLOW "\nNotice: Shorter keys come first, then alphabetical within same length!\n" COLOR_RESET);

    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}

/**
 * Demo 5: Multi-field keys
 */
void demo_multi_field() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   Demo 5: Multi-Field Keys (Timestamp:UserID)                 ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    MDB_env *env;
    MDB_dbi dbi;

    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024);
    mdb_env_open(env, "./custom_multifield", 0, 0664);

    open_custom_db(env, "./custom_multifield", compare_timestamp_userid, &dbi);

    /* Format: "timestamp:userid" */
    const char* keys[] = {"1000:alice", "500:bob", "1000:charlie",
                          "750:dave", "500:alice", "1250:eve"};
    const char* values[] = {"msg1", "msg2", "msg3", "msg4", "msg5", "msg6"};

    insert_data(env, dbi, keys, values, 6);
    display_db(env, dbi, "Multi-Field Order (timestamp first, then userID)");

    printf(COLOR_YELLOW "\nNotice: Sorted by timestamp first, then user ID within same timestamp!\n" COLOR_RESET);

    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}

/**
 * Explain comparator requirements
 */
void explain_comparators() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║         Custom Comparator Requirements                        ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    printf("\n" COLOR_BOLD "Comparator Signature:" COLOR_RESET "\n");
    printf("  int compare(const MDB_val *a, const MDB_val *b);\n");

    printf("\n" COLOR_BOLD "Return Values:" COLOR_RESET "\n");
    printf("  • " COLOR_GREEN "< 0" COLOR_RESET " if a < b\n");
    printf("  • " COLOR_GREEN "0" COLOR_RESET "   if a == b\n");
    printf("  • " COLOR_GREEN "> 0" COLOR_RESET " if a > b\n");

    printf("\n" COLOR_BOLD "Important Properties:" COLOR_RESET "\n");
    printf("  1. " COLOR_GREEN "Consistency:" COLOR_RESET "\n");
    printf("     - Must always return same result for same inputs\n");
    printf("     - Result cannot change over time\n");

    printf("\n  2. " COLOR_GREEN "Transitivity:" COLOR_RESET "\n");
    printf("     - If a < b and b < c, then a < c\n");

    printf("\n  3. " COLOR_GREEN "Symmetry:" COLOR_RESET "\n");
    printf("     - If compare(a,b) < 0, then compare(b,a) > 0\n");

    printf("\n  4. " COLOR_GREEN "Equality:" COLOR_RESET "\n");
    printf("     - compare(a,b) == 0 implies compare(b,a) == 0\n");

    printf("\n" COLOR_BOLD "When to Use Custom Comparators:" COLOR_RESET "\n");
    printf("  • Numeric strings (\"100\" vs \"20\")\n");
    printf("  • Reverse ordering\n");
    printf("  • Case-insensitive sorting\n");
    printf("  • Locale-specific ordering\n");
    printf("  • Composite keys (multiple fields)\n");
    printf("  • Binary data with special ordering\n");

    printf("\n" COLOR_BOLD "Caveats:" COLOR_RESET "\n");
    printf("  • Comparator must be set before any data is inserted\n");
    printf("  • Cannot change comparator after data exists\n");
    printf("  • Comparator is called frequently - must be fast\n");
    printf("  • Must handle any key value in the database\n");
}

int main() {
    printf(COLOR_CYAN "\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     LMDB Custom Comparison Function Demo                   ║\n");
    printf("║                    Day 5 - Advanced Features               ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf(COLOR_RESET);

    explain_comparators();

    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter to see demos..." COLOR_RESET);
    getchar();

    demo_numeric();
    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter..." COLOR_RESET);
    getchar();

    demo_reverse();
    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter..." COLOR_RESET);
    getchar();

    demo_case_insensitive();
    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter..." COLOR_RESET);
    getchar();

    demo_length_first();
    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter..." COLOR_RESET);
    getchar();

    demo_multi_field();

    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║                    SUMMARY                                    ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    printf("\n" COLOR_YELLOW "Key Takeaways:" COLOR_RESET "\n");
    printf("  • Custom comparators give you control over sort order\n");
    printf("  • Must be consistent, transitive, and symmetric\n");
    printf("  • Set comparator before inserting any data\n");
    printf("  • Complex keys can be handled with custom logic\n");
    printf("  • Performance matters - comparator is called often\n");

    printf("\n" COLOR_YELLOW "Common Patterns:" COLOR_RESET "\n");
    printf("  • Numeric strings: Parse and compare integers\n");
    printf("  • Reverse order: Negate comparison result\n");
    printf("  • Case-insensitive: Use tolower/toupper\n");
    printf("  • Multi-field: Compare field by field\n");
    printf("  • Length-based: Compare sizes first, then content\n");

    printf(COLOR_GREEN "\n✓ custom_compare completed!\n" COLOR_RESET);
    printf("  Test databases created:\n");
    printf("    ./custom_numeric/\n");
    printf("    ./custom_reverse/\n");
    printf("    ./custom_nocase/\n");
    printf("    ./custom_length/\n");
    printf("    ./custom_multifield/\n");

    return 0;
}
