/**
 * txn_tracker.c - Day 6: Transaction Lifecycle Tracker
 *
 * This program tracks and visualizes the complete lifecycle of LMDB
 * transactions, showing their states and transitions.
 *
 * Learning Objectives:
 * - Understand transaction states (initial, active, committed, aborted)
 * - Visualize transaction lifecycle
 * - Track nested transaction behavior
 * - Monitor transaction IDs and reader table
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

/* Transaction states */
typedef enum {
    TXN_NONE,
    TXN_CREATED,
    TXN_ACTIVE,
    TXN_COMMITTED,
    TXN_ABORTED
} txn_state_t;

/* Transaction record */
typedef struct {
    MDB_txn* handle;
    txn_state_t state;
    size_t txn_id;
    int is_readonly;
    int is_nested;
    struct timespec start_time;
    struct timespec end_time;
    size_t num_ops;
} txn_record_t;

/* Transaction tracker */
typedef struct {
    txn_record_t txns[100];
    int count;
    size_t next_id;
} txn_tracker_t;

static txn_tracker_t g_tracker = {0};

/**
 * Get current time as string
 */
void get_time_str(char* buf, size_t len) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    snprintf(buf, len, "%ld.%03ld", ts.tv_sec % 10000, ts.tv_nsec / 1000000);
}

/**
 * Create a new transaction record
 */
txn_record_t* track_txn(MDB_txn* txn, int is_readonly, int is_nested) {
    if (g_tracker.count >= 100) return NULL;

    txn_record_t* rec = &g_tracker.txns[g_tracker.count++];
    rec->handle = txn;
    rec->state = TXN_CREATED;
    rec->txn_id = g_tracker.next_id++;
    rec->is_readonly = is_readonly;
    rec->is_nested = is_nested;
    rec->num_ops = 0;
    clock_gettime(CLOCK_REALTIME, &rec->start_time);

    char tbuf[64];
    get_time_str(tbuf, sizeof(tbuf));

    printf(COLOR_GREEN "[TXN#%zu CREATED] %s" COLOR_RESET "\n",
           rec->txn_id, tbuf);
    printf("    Type: %s\n", is_readonly ? "Read-Only" : "Read-Write");
    printf("    Nested: %s\n", is_nested ? "Yes" : "No");

    return rec;
}

/**
 * Update transaction state
 */
void update_txn_state(size_t txn_id, txn_state_t state) {
    for (int i = 0; i < g_tracker.count; i++) {
        if (g_tracker.txns[i].txn_id == txn_id) {
            g_tracker.txns[i].state = state;
            clock_gettime(CLOCK_REALTIME, &g_tracker.txns[i].end_time);

            char tbuf[64];
            get_time_str(tbuf, sizeof(tbuf));

            const char* state_str;
            const char* color;
            switch (state) {
                case TXN_ACTIVE:
                    state_str = "ACTIVE";
                    color = COLOR_CYAN;
                    break;
                case TXN_COMMITTED:
                    state_str = "COMMITTED";
                    color = COLOR_GREEN;
                    break;
                case TXN_ABORTED:
                    state_str = "ABORTED";
                    color = COLOR_RED;
                    break;
                default:
                    state_str = "UNKNOWN";
                    color = COLOR_RESET;
            }

            printf("%s[TXN#%zu %s] %s%s\n",
                   color, txn_id, state_str, tbuf, COLOR_RESET);
            return;
        }
    }
}

/**
 * Track an operation within a transaction
 */
void track_op(size_t txn_id, const char* op_type, const char* key) {
    for (int i = 0; i < g_tracker.count; i++) {
        if (g_tracker.txns[i].txn_id == txn_id) {
            g_tracker.txns[i].num_ops++;
            printf("  [OP#%zu] %s(key='%s')\n",
                   g_tracker.txns[i].num_ops, op_type, key);
            return;
        }
    }
}

/**
 * Get transaction duration in milliseconds
 */
double get_duration_ms(const txn_record_t* rec) {
    long ns = (rec->end_time.tv_sec - rec->start_time.tv_sec) * 1000000000L +
              (rec->end_time.tv_nsec - rec->start_time.tv_nsec);
    return ns / 1000000.0;
}

/**
 * Print transaction summary
 */
void print_txn_summary() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║           TRANSACTION SUMMARY                                 ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("┌──────────┬──────────┬───────────┬─────────┬──────────┬──────────┐\n");
    printf("│ TXN ID   │ Type     │ State     │ Nested  │ Ops      │ Duration │\n");
    printf("├──────────┼──────────┼───────────┼─────────┼──────────┼──────────┤\n");

    for (int i = 0; i < g_tracker.count; i++) {
        txn_record_t* rec = &g_tracker.txns[i];

        const char* type = rec->is_readonly ? "RO" : "RW";
        const char* state;
        const char* color;
        switch (rec->state) {
            case TXN_COMMITTED: state = "COMMITTED"; color = COLOR_GREEN; break;
            case TXN_ABORTED:   state = "ABORTED";   color = COLOR_RED; break;
            case TXN_ACTIVE:    state = "ACTIVE";    color = COLOR_CYAN; break;
            default:            state = "UNKNOWN";   color = COLOR_RESET; break;
        }

        const char* nested = rec->is_nested ? "Yes" : "No";
        double duration = get_duration_ms(rec);

        printf("│ %-8zu │ %-8s │" COLOR_RESET "%s%s%-9s" COLOR_RESET " │ %-7s │ %-8zu │ %7.2fms │\n",
               rec->txn_id, type, color, "", state, nested, rec->num_ops, duration);
    }

    printf("└──────────┴──────────┴───────────┴─────────┴──────────┴──────────┘\n");
}

/**
 * Visualize transaction lifecycle
 */
void visualize_lifecycle() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║         TRANSACTION LIFECYCLE                                  ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("    ┌─────────┐      ┌─────────┐      ┌──────────┐      ┌─────────┐\n");
    printf("    │  BEGIN  │ ───> │  ACTIVE │ ───> │  COMMIT  │ ───> │ FINISH  │\n");
    printf("    └─────────┘      └─────────┘      └──────────┘      └─────────┘\n");
    printf("         │                                   │\n");
    printf("         │                                   │\n");
    printf("         v                                   v\n");
    printf("    ┌─────────┐                          ┌─────────┐\n");
    printf("    │ CREATED │                          │ ABORT   │\n");
    printf("    └─────────┘                          └─────────┘\n");
    printf("\n");

    printf(COLOR_YELLOW "State Descriptions:" COLOR_RESET "\n");
    printf("  " COLOR_GREEN "CREATED" COLOR_RESET "  - Transaction initialized, not yet active\n");
    printf("  " COLOR_CYAN "ACTIVE" COLOR_RESET "   - Transaction is processing operations\n");
    printf("  " COLOR_GREEN "COMMITTED" COLOR_RESET " - Changes durably persisted\n");
    printf("  " COLOR_RED "ABORTED" COLOR_RESET "   - Changes discarded, state rolled back\n");
    printf("\n");

    printf(COLOR_YELLOW "Transaction Types:" COLOR_RESET "\n");
    printf("  • " COLOR_CYAN "Read-Only (RO): " COLOR_RESET " Can only read data\n");
    printf("  • " COLOR_CYAN "Read-Write (RW): " COLOR_RESET " Can read and modify data\n");
    printf("  • " COLOR_CYAN "Nested: " COLOR_RESET " Child of parent transaction\n");
    printf("\n");

    printf(COLOR_YELLOW "Rules:" COLOR_RESET "\n");
    printf("  1. Only ONE read-write transaction can be active at a time\n");
    printf("  2. Multiple read-only transactions can run concurrently\n");
    printf("  3. Nested transactions inherit parent's view\n");
    printf("  4. Child commits merge into parent (not to DB directly)\n");
    printf("  5. Child abort discards only child's changes\n");
}

/**
 * Demo 1: Simple read-write transaction
 */
void demo_simple_txn() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   Demo 1: Simple Read-Write Transaction                     ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;

    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024);
    mdb_env_open(env, "./txn_tracker_demo1", 0, 0664);

    printf("\n--- Beginning Transaction ---\n");
    mdb_txn_begin(env, NULL, 0, &txn);
    txn_record_t* rec = track_txn(mdb_txn_id(txn), 0, 0);
    update_txn_state(rec->txn_id, TXN_ACTIVE);

    mdb_dbi_open(txn, NULL, 0, &dbi);

    printf("\n--- Performing Operations ---\n");
    for (int i = 0; i < 3; i++) {
        char key[32], value[32];
        snprintf(key, sizeof(key), "key%d", i);
        snprintf(value, sizeof(value), "value%d", i);

        MDB_val k = { .mv_size = strlen(key), .mv_data = key };
        MDB_val v = { .mv_size = strlen(value), .mv_data = value };

        mdb_put(txn, dbi, &k, &v, 0);
        track_op(rec->txn_id, "PUT", key);
    }

    printf("\n--- Committing Transaction ---\n");
    mdb_txn_commit(txn);
    update_txn_state(rec->txn_id, TXN_COMMITTED);

    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}

/**
 * Demo 2: Nested transactions
 */
void demo_nested_txn() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   Demo 2: Nested Transactions                                ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    MDB_env *env;
    MDB_txn *parent, *child;
    MDB_dbi dbi;

    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024);
    mdb_env_open(env, "./txn_tracker_demo2", 0, 0664);

    printf("\n--- Beginning Parent Transaction ---\n");
    mdb_txn_begin(env, NULL, 0, &parent);
    mdb_dbi_open(parent, NULL, 0, &dbi);
    txn_record_t* prec = track_txn(parent, 0, 0);
    update_txn_state(prec->txn_id, TXN_ACTIVE);

    /* Add data in parent */
    MDB_val k = { .mv_size = 5, .mv_data = "parent" };
    MDB_val v = { .mv_size = 11, .mv_data = "parent_data" };
    mdb_put(parent, dbi, &k, &v, 0);
    track_op(prec->txn_id, "PUT", "parent");

    printf("\n--- Beginning Child Transaction ---\n");
    mdb_txn_begin(env, parent, 0, &child);
    txn_record_t* crec = track_txn(child, 0, 1);
    update_txn_state(crec->txn_id, TXN_ACTIVE);

    /* Child sees parent's data */
    MDB_val data;
    mdb_get(child, dbi, &k, &data);
    track_op(crec->txn_id, "GET", "parent");

    /* Add data in child */
    k.mv_data = "child";
    k.mv_size = 5;
    v.mv_data = "child_data";
    v.mv_size = 10;
    mdb_put(child, dbi, &k, &v, 0);
    track_op(crec->txn_id, "PUT", "child");

    printf("\n--- Committing Child Transaction ---\n");
    mdb_txn_commit(child);
    update_txn_state(crec->txn_id, TXN_COMMITTED);
    printf(COLOR_YELLOW "Child's changes are now visible to parent" COLOR_RESET "\n");

    /* Parent can now see child's data */
    k.mv_data = "child";
    k.mv_size = 5;
    mdb_get(parent, dbi, &k, &data);
    track_op(prec->txn_id, "GET", "child");

    printf("\n--- Committing Parent Transaction ---\n");
    mdb_txn_commit(parent);
    update_txn_state(prec->txn_id, TXN_COMMITTED);
    printf(COLOR_YELLOW "Both parent and child changes are now durable" COLOR_RESET "\n");

    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}

/**
 * Demo 3: Abort scenario
 */
void demo_abort_txn() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   Demo 3: Transaction Abort                                  ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;

    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024);
    mdb_env_open(env, "./txn_tracker_demo3", 0, 0664);

    /* First, add some data */
    mdb_txn_begin(env, NULL, 0, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);
    MDB_val k = { .mv_size = 7, .mv_data = "initial" };
    MDB_val v = { .mv_size = 5, .mv_data = "data1" };
    mdb_put(txn, dbi, &k, &v, 0);
    mdb_txn_commit(txn);

    printf("\n--- Beginning Transaction (will be aborted) ---\n");
    mdb_txn_begin(env, NULL, 0, &txn);
    txn_record_t* rec = track_txn(mdb_txn_id(txn), 0, 0);
    update_txn_state(rec->txn_id, TXN_ACTIVE);

    /* Modify data */
    k.mv_data = "initial";
    k.mv_size = 7;
    v.mv_data = "data2";
    v.mv_size = 5;
    mdb_put(txn, dbi, &k, &v, 0);
    track_op(rec->txn_id, "PUT", "initial");

    /* Add new data */
    k.mv_data = "newkey";
    k.mv_size = 6;
    v.mv_data = "newdata";
    v.mv_size = 7;
    mdb_put(txn, dbi, &k, &v, 0);
    track_op(rec->txn_id, "PUT", "newkey");

    printf("\n--- Aborting Transaction ---\n");
    mdb_txn_abort(txn);
    update_txn_state(rec->txn_id, TXN_ABORTED);
    printf(COLOR_RED "All changes in this transaction have been discarded" COLOR_RESET "\n");

    /* Verify data is unchanged */
    printf("\n--- Verifying Data After Abort ---\n");
    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    k.mv_data = "initial";
    k.mv_size = 7;
    int rc = mdb_get(txn, dbi, &k, &v);
    if (rc == 0) {
        printf("  'initial' = '%.*s' " COLOR_GREEN "(unchanged)" COLOR_RESET "\n",
               (int)v.mv_size, (char*)v.mv_data);
    }

    k.mv_data = "newkey";
    k.mv_size = 6;
    rc = mdb_get(txn, dbi, &k, &v);
    if (rc == MDB_NOTFOUND) {
        printf("  'newkey' = " COLOR_RED "(not found)" COLOR_RESET "\n");
    }
    mdb_txn_abort(txn);

    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}

/**
 * Demo 4: Read-only transaction
 */
void demo_readonly_txn() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║   Demo 4: Read-Only Transaction                               ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    MDB_env *env;
    MDB_txn *txn_w, *txn_r;
    MDB_dbi dbi;

    mdb_env_create(&env);
    mdb_env_set_mapsize(env, 1024 * 1024);
    mdb_env_open(env, "./txn_tracker_demo4", 0, 0664);

    /* Setup data */
    mdb_txn_begin(env, NULL, 0, &txn_w);
    mdb_dbi_open(txn_w, NULL, 0, &dbi);
    for (int i = 0; i < 5; i++) {
        char key[32], value[32];
        snprintf(key, sizeof(key), "key%d", i);
        snprintf(value, sizeof(value), "value%d", i);
        MDB_val k = { .mv_size = strlen(key), .mv_data = key };
        MDB_val v = { .mv_size = strlen(value), .mv_data = value };
        mdb_put(txn_w, dbi, &k, &v, 0);
    }
    mdb_txn_commit(txn_w);

    printf("\n--- Beginning Read-Only Transaction ---\n");
    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn_r);
    txn_record_t* rec = track_txn(mdb_txn_id(txn_r), 1, 0);
    update_txn_state(rec->txn_id, TXN_ACTIVE);

    /* Read operations */
    MDB_cursor *cursor;
    mdb_cursor_open(txn_r, dbi, &cursor);

    MDB_val key, data;
    int count = 0;
    while (mdb_cursor_get(cursor, &key, &data, MDB_NEXT) == 0 && count < 5) {
        printf("  [READ] key='%.*s' value='%.*s'\n",
               (int)key.mv_size, (char*)key.mv_data,
               (int)data.mv_size, (char*)data.mv_data);
        count++;
    }

    mdb_cursor_close(cursor);

    printf("\n--- Read-Only Transaction Complete ---\n");
    mdb_txn_abort(txn_r);  /* Can abort or commit - same for RO */
    update_txn_state(rec->txn_id, TXN_COMMITTED);
    printf(COLOR_GREEN "Read-only transactions don't need commit (abort is fine)" COLOR_RESET "\n");

    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}

int main() {
    printf(COLOR_CYAN "\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     LMDB Transaction Lifecycle Tracker                     ║\n");
    printf("║                    Day 6 - Transaction Management          ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf(COLOR_RESET);

    visualize_lifecycle();

    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter to see Demo 1..." COLOR_RESET);
    getchar();
    demo_simple_txn();

    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter to see Demo 2..." COLOR_RESET);
    getchar();
    demo_nested_txn();

    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter to see Demo 3..." COLOR_RESET);
    getchar();
    demo_abort_txn();

    printf(COLOR_CYAN "\n" COLOR_BOLD "Press Enter to see Demo 4..." COLOR_RESET);
    getchar();
    demo_readonly_txn();

    print_txn_summary();

    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║                    SUMMARY                                    ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    printf("\n" COLOR_YELLOW "Transaction Lifecycle:" COLOR_RESET "\n");
    printf("  1. BEGIN → CREATED → ACTIVE\n");
    printf("  2. Perform operations (GET, PUT, DEL)\n");
    printf("  3. COMMIT or ABORT\n");
    printf("  4. FINISH (cleanup)\n");

    printf("\n" COLOR_YELLOW "Important Notes:" COLOR_RESET "\n");
    printf("  • Transactions provide ACID guarantees\n");
    printf("  • Read-only transactions can run concurrently\n");
    printf("  • Only ONE write transaction at a time\n");
    printf("  • Nested transactions merge into parent on commit\n");
    printf("  • Always commit or abort (never leave dangling)\n");

    printf(COLOR_GREEN "\n✓ txn_tracker completed!\n" COLOR_RESET);
    printf("  Demo databases created:\n");
    printf("    ./txn_tracker_demo1/\n");
    printf("    ./txn_tracker_demo2/\n");
    printf("    ./txn_tracker_demo3/\n");
    printf("    ./txn_tracker_demo4/\n");

    return 0;
}
