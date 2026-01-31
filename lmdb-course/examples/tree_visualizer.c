/**
 * tree_visualizer.c - Day 5: B+Tree Visualization Tool
 *
 * This program visualizes the B+tree structure used by LMDB.
 * It demonstrates how the tree is organized and how data flows through it.
 *
 * Learning Objectives:
 * - Understand B+tree structure and organization
 * - Visualize tree balancing operations
 * - See how searches traverse the tree
 * - Understand split/merge operations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* Maximum tree depth to visualize */
#define MAX_DEPTH 5

/* Tree node representation */
typedef struct tree_node {
    int is_leaf;
    int num_keys;
    char keys[10][32];
    struct tree_node* children[11];
    size_t page_num;
} tree_node_t;

/* B+tree statistics */
typedef struct {
    size_t total_nodes;
    size_t leaf_nodes;
    size_t branch_nodes;
    size_t total_keys;
    size_t tree_depth;
    size_t min_keys;
    size_t max_keys;
} tree_stats_t;

/**
 * Create a simple example tree for visualization
 */
tree_node_t* create_sample_tree() {
    /* This creates a simple B+tree structure for demonstration */
    tree_node_t* root = calloc(1, sizeof(tree_node_t));
    root->is_leaf = 0;
    root->num_keys = 3;
    root->page_num = 1;
    strcpy(root->keys[0], "cherry");
    strcpy(root->keys[1], "pear");
    strcpy(root->keys[2], "zebra");

    /* Left child (branch) */
    tree_node_t* left = calloc(1, sizeof(tree_node_t));
    left->is_leaf = 0;
    left->num_keys = 2;
    left->page_num = 2;
    strcpy(left->keys[0], "banana");
    strcpy(left->keys[1], "apple");
    root->children[0] = left;

    /* Left-left (leaf) */
    tree_node_t* left_left = calloc(1, sizeof(tree_node_t));
    left_left->is_leaf = 1;
    left_left->num_keys = 4;
    left_left->page_num = 3;
    strcpy(left_left->keys[0], "apricot");
    strcpy(left_left->keys[1], "avocado");
    strcpy(left_left->keys[2], "apple");
    strcpy(left_left->keys[3], "artichoke");
    left->children[0] = left_left;

    /* Left-right (leaf) */
    tree_node_t* left_right = calloc(1, sizeof(tree_node_t));
    left_right->is_leaf = 1;
    left_right->num_keys = 4;
    left_right->page_num = 4;
    strcpy(left_right->keys[0], "blackberry");
    strcpy(left_right->keys[1], "blueberry");
    strcpy(left_right->keys[2], "banana");
    strcpy(left_right->keys[3], "berry");
    left->children[1] = left_right;

    /* Middle child (leaf) */
    tree_node_t* middle = calloc(1, sizeof(tree_node_t));
    middle->is_leaf = 1;
    middle->num_keys = 4;
    middle->page_num = 5;
    strcpy(middle->keys[0], "coconut");
    strcpy(middle->keys[1], "cherry");
    strcpy(middle->keys[2], "celery");
    strcpy(middle->keys[3], "carrot");
    root->children[1] = middle;

    /* Right child (branch) */
    tree_node_t* right = calloc(1, sizeof(tree_node_t));
    right->is_leaf = 0;
    right->num_keys = 2;
    right->page_num = 6;
    strcpy(right->keys[0], "orange");
    strcpy(right->keys[1], "plum");
    root->children[2] = right;

    /* Right-left (leaf) */
    tree_node_t* right_left = calloc(1, sizeof(tree_node_t));
    right_left->is_leaf = 1;
    right_left->num_keys = 4;
    right_left->page_num = 7;
    strcpy(right_left->keys[0], "peach");
    strcpy(right_left->keys[1], "pear");
    strcpy(right_left->keys[2], "papaya");
    strcpy(right_left->keys[3], "orange");
    right->children[0] = right_left;

    /* Right-right (leaf) */
    tree_node_t* right_right = calloc(1, sizeof(tree_node_t));
    right_right->is_leaf = 1;
    right_right->num_keys = 4;
    right_right->page_num = 8;
    strcpy(right_right->keys[0], "watermelon");
    strcpy(right_right->keys[1], "zebra");
    strcpy(right_right->keys[2], "yam");
    strcpy(right_right->keys[3], "zucchini");
    right->children[1] = right_right;

    return root;
}

/**
 * Print a tree node
 */
void print_node(tree_node_t* node, int depth) {
    if (!node) return;

    /* Indentation */
    for (int i = 0; i < depth; i++) printf("    ");

    /* Node type */
    if (node->is_leaf) {
        printf(COLOR_GREEN "L" COLOR_RESET);
    } else {
        printf(COLOR_CYAN "B" COLOR_RESET);
    }

    /* Page number and keys */
    printf("[p%zu] ", node->page_num);
    for (int i = 0; i < node->num_keys; i++) {
        if (i > 0) printf(", ");
        printf("%s", node->keys[i]);
    }

    printf("\n");
}

/**
 * Print tree structure
 */
void print_tree(tree_node_t* node, int depth) {
    if (!node) return;

    print_node(node, depth);

    if (!node->is_leaf) {
        for (int i = 0; i <= node->num_keys; i++) {
            print_tree(node->children[i], depth + 1);
        }
    }
}

/**
 * Visualize tree with ASCII art
 */
void visualize_tree_ascii() {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║              B+TREE STRUCTURE VISUALIZATION                ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    /* Level 0: Root */
    printf("                    " COLOR_CYAN "[ROOT - Branch]" COLOR_RESET "\n");
    printf("                    Keys: cherry, pear, zebra\n");
    printf("                            │\n");
    printf("          ┌─────────────────┼─────────────────┐\n");
    printf("          ▼                 ▼                 ▼\n");

    /* Level 1: Branch nodes and leaves */
    printf("  " COLOR_CYAN "[Branch]" COLOR_RESET "        " COLOR_GREEN "[Leaf]" COLOR_RESET "         " COLOR_CYAN "[Branch]" COLOR_RESET "\n");
    printf("  apple,banana       cherry...         orange,plum\n");
    printf("      │                                   │\n");
    printf("      ├───────────┐               ┌───────┴───────┐\n");
    printf("      ▼           ▼               ▼               ▼\n");

    /* Level 2: Leaves */
    printf(" " COLOR_GREEN "[Leaf]" COLOR_RESET "     " COLOR_GREEN "[Leaf]" COLOR_RESET "        " COLOR_GREEN "[Leaf]" COLOR_RESET "        " COLOR_GREEN "[Leaf]" COLOR_RESET "\n");
    printf(" apricot...   blackberry...    coconut...      peach...\n");
    printf(" avocado     blueberry        celery          pear\n");
    printf(" apple       banana           carrot          papaya\n");
    printf(" artichoke   berry                            orange\n");
    printf("\n");

    printf(COLOR_YELLOW "Legend:" COLOR_RESET "\n");
    printf("  " COLOR_CYAN "B" COLOR_RESET " = Branch page (internal node)\n");
    printf("  " COLOR_GREEN "L" COLOR_RESET " = Leaf page (contains data)\n");
    printf("\n");
}

/**
 * Show search operation in B+tree
 */
void visualize_search(const char* search_key) {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║          SEARCH OPERATION: '%s'                       ║\n", search_key);
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("Step 1: Start at root\n");
    printf("        Root keys: [cherry, pear, zebra]\n");
    printf("\n");

    /* Determine path */
    int cmp = strcmp(search_key, "cherry");
    if (cmp < 0) {
        printf("Step 2: " COLOR_YELLOW "%s < cherry" COLOR_RESET "\n", search_key);
        printf("        Go left → Branch page 2\n");
        printf("        Branch keys: [apple, banana]\n");
        printf("\n");

        cmp = strcmp(search_key, "banana");
        if (cmp < 0) {
            printf("Step 3: " COLOR_YELLOW "%s < banana" COLOR_RESET "\n", search_key);
            printf("        Go left → Leaf page 3\n");
            printf("        Leaf keys: [apricot, avocado, apple, artichoke]\n");
            printf("\n");

            /* Check if found */
            int found = 0;
            const char* keys[] = {"apricot", "avocado", "apple", "artichoke"};
            for (int i = 0; i < 4; i++) {
                if (strcmp(search_key, keys[i]) == 0) {
                    printf("Step 4: " COLOR_GREEN "FOUND!" COLOR_RESET " Value retrieved.\n");
                    found = 1;
                    break;
                }
            }
            if (!found) {
                printf("Step 4: " COLOR_RED "NOT FOUND" COLOR_RESET " (key doesn't exist)\n");
            }
        } else {
            printf("Step 3: " COLOR_YELLOW "%s >= banana" COLOR_RESET "\n", search_key);
            printf("        Go right → Leaf page 4\n");
            printf("        Leaf keys: [blackberry, blueberry, banana, berry]\n");
            printf("\n");
            printf("Step 4: " COLOR_GREEN "FOUND!" COLOR_RESET " Value retrieved.\n");
        }
    } else if (cmp == 0) {
        printf("Step 2: " COLOR_YELLOW "%s == cherry" COLOR_RESET "\n", search_key);
        printf("        Go middle → Leaf page 5\n");
        printf("        Leaf keys: [coconut, cherry, celery, carrot]\n");
        printf("\n");
        printf("Step 3: " COLOR_GREEN "FOUND!" COLOR_RESET " Value retrieved.\n");
    } else {
        cmp = strcmp(search_key, "pear");
        if (cmp < 0) {
            printf("Step 2: " COLOR_YELLOW "cherry < %s < pear" COLOR_RESET "\n", search_key);
            printf("        Go middle → Leaf page 5\n");
            printf("        Leaf keys: [coconut, cherry, celery, carrot]\n");
            printf("\n");
            printf("Step 3: " COLOR_RED "NOT FOUND" COLOR_RESET " (would be in range if exists)\n");
        } else {
            cmp = strcmp(search_key, "zebra");
            if (cmp < 0) {
                printf("Step 2: " COLOR_YELLOW "pear <= %s < zebra" COLOR_RESET "\n", search_key);
                printf("        Go right → Branch page 6\n");
                printf("        Branch keys: [orange, plum]\n");
                printf("\n");
                printf("Step 3: Navigate to appropriate leaf\n");
                printf("        " COLOR_GREEN "Retrieve value" COLOR_RESET "\n");
            } else {
                printf("Step 2: " COLOR_YELLOW "%s >= zebra" COLOR_RESET "\n", search_key);
                printf("        Go right → Branch page 6\n");
                printf("        " COLOR_RED "Key out of range" COLOR_RESET "\n");
            }
        }
    }

    printf("\n" COLOR_YELLOW "Search Complexity:" COLOR_RESET " O(log n)\n");
    printf("  • 3 levels → at most 3 page reads\n");
    printf("  • Each comparison halves search space\n");
    printf("  • Tree depth determines maximum operations\n");
}

/**
 * Show insert operation
 */
void visualize_insert(const char* new_key) {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║          INSERT OPERATION: '%s'                         ║\n", new_key);
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("Step 1: Find correct leaf page\n");
    printf("        Traverse tree (same as search)\n");
    printf("\n");

    printf("Step 2: Check if leaf has space\n");
    printf("        Leaf has room? " COLOR_GREEN "YES" COLOR_RESET " → Insert directly\n");
    printf("        Leaf is full?  " COLOR_YELLOW "SPLIT" COLOR_RESET " required\n");
    printf("\n");

    printf("Step 3: Insert into sorted position\n");
    printf("        Leaf keys: [apricot, avocado, apple, artichoke]\n");
    printf("        Insert '%s' at position 2\n", new_key);
    printf("        New order: [apricot, avocado, " COLOR_GREEN "%s" COLOR_RESET ", apple, artichoke]\n", new_key);
    printf("\n");

    printf(COLOR_YELLOW "Split Operation (when leaf is full):" COLOR_RESET "\n");
    printf("  1. Allocate new leaf page\n");
    printf("  2. Copy half the keys to new page\n");
    printf("  3. Insert separator key into parent\n");
    printf("  4. If parent is full, split parent too\n");
    printf("  5. May propagate all the way to root\n");
    printf("  6. If root splits, tree grows by 1 level\n");
    printf("\n");

    printf(COLOR_YELLOW "B+tree Balancing:" COLOR_RESET "\n");
    printf("  • All leaves at same depth\n");
    printf("  • Each node 50-100% full (except root)\n");
    printf("  • Splits and merges maintain balance\n");
    printf("  • Guarantees O(log n) operations\n");
}

/**
 * Show delete operation
 */
void visualize_delete(const char* del_key) {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║          DELETE OPERATION: '%s'                         ║\n", del_key);
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("Step 1: Find key in tree\n");
    printf("        Navigate to correct leaf\n");
    printf("\n");

    printf("Step 2: Remove from leaf\n");
    printf("        Leaf keys: [coconut, cherry, celery, carrot]\n");
    printf("        Remove 'cherry'\n");
    printf("        After:     [coconut, celery, carrot]\n");
    printf("\n");

    printf("Step 3: Check if leaf is too empty\n");
    printf("        Keys >= 50% capacity? " COLOR_GREEN "OK" COLOR_RESET " → Done\n");
    printf("        Keys < 50% capacity?  " COLOR_YELLOW "MERGE" COLOR_RESET " or redistribute\n");
    printf("\n");

    printf(COLOR_YELLOW "Merge/Redistribute:" COLOR_RESET "\n");
    printf("  1. Try to borrow key from sibling\n");
    printf("  2. If sibling also minimal, merge pages\n");
    printf("  3. Remove separator from parent\n");
    printf("  4. May cascade up to root\n");
    printf("  5. Root can shrink (tree depth decreases)\n");
    printf("\n");

    printf(COLOR_YELLOW "LMDB Optimization:" COLOR_RESET "\n");
    printf("  • LMDB rarely merges (COW makes it expensive)\n");
    printf("  • Space reclaimed by free list\n");
    printf("  • Old pages freed after all readers finish\n");
}

/**
 * Calculate tree statistics
 */
void calculate_stats(tree_node_t* node, tree_stats_t* stats, int depth) {
    if (!node) return;

    stats->total_nodes++;
    stats->total_keys += node->num_keys;
    if (depth > stats->tree_depth) {
        stats->tree_depth = depth;
    }

    if (node->is_leaf) {
        stats->leaf_nodes++;
    } else {
        stats->branch_nodes++;
        for (int i = 0; i <= node->num_keys; i++) {
            calculate_stats(node->children[i], stats, depth + 1);
        }
    }

    if (stats->min_keys == 0 || node->num_keys < stats->min_keys) {
        stats->min_keys = node->num_keys;
    }
    if (node->num_keys > stats->max_keys) {
        stats->max_keys = node->num_keys;
    }
}

/**
 * Print tree statistics
 */
void print_tree_stats(tree_stats_t* stats) {
    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║              B+TREE STATISTICS                              ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");

    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│  Structure Metrics                                      │\n");
    printf("├─────────────────────────────────────────────────────────┤\n");
    printf("│  Total Nodes:        %6zu                             │\n", stats->total_nodes);
    printf("│  Branch Nodes:       %6zu                             │\n", stats->branch_nodes);
    printf("│  Leaf Nodes:         %6zu                             │\n", stats->leaf_nodes);
    printf("│  Tree Depth:         %6zu levels                       │\n", stats->tree_depth + 1);
    printf("│  Total Keys:         %6zu                             │\n", stats->total_keys);
    printf("├─────────────────────────────────────────────────────────┤\n");
    printf("│  Capacity Metrics                                        │\n");
    printf("├─────────────────────────────────────────────────────────┤\n");
    printf("│  Min keys/node:       %4zu                             │\n", stats->min_keys);
    printf("│  Max keys/node:       %4zu                             │\n", stats->max_keys);
    printf("│  Avg keys/node:       %4.1f                             │\n",
           (float)stats->total_keys / stats->total_nodes);
    printf("└─────────────────────────────────────────────────────────┘\n");
    printf("\n");

    printf(COLOR_YELLOW "Performance Implications:" COLOR_RESET "\n");
    printf("  • Max comparisons per search: %d\n", stats->tree_depth + 1);
    printf("  • Page reads per operation:   %d\n", stats->tree_depth + 1);
    printf("  • Tree is balanced:           YES\n");
    printf("  • Space utilization:         %.0f%%\n",
           100.0 * stats->total_keys / (stats->total_nodes * 5));
}

/**
 * Create real database and show statistics
 */
void create_real_database() {
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;

    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║         CREATING REAL DATABASE                                 ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    if (mdb_env_create(&env) != 0) {
        PRINT_ERROR("Failed to create environment");
        return;
    }

    mdb_env_set_mapsize(env, 1024 * 1024 * 100);
    mdb_env_set_maxdbs(env, 1);

    if (mdb_env_open(env, "./treeviz_db", 0, 0664) != 0) {
        PRINT_ERROR("Failed to open environment");
        mdb_env_close(env);
        return;
    }

    /* Insert data */
    mdb_txn_begin(env, NULL, 0, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);

    const char* fruits[] = {
        "apple", "apricot", "avocado", "banana", "blackberry",
        "blueberry", "cherry", "coconut", "grape", "kiwi",
        "lemon", "mango", "orange", "peach", "pear",
        "plum", "raspberry", "strawberry", "watermelon", "zucchini"
    };

    for (int i = 0; i < 20; i++) {
        char value[64];
        snprintf(value, sizeof(value), "value_for_%s", fruits[i]);

        MDB_val k = { .mv_size = strlen(fruits[i]), .mv_data = (void*)fruits[i] };
        MDB_val v = { .mv_size = strlen(value), .mv_data = value };
        mdb_put(txn, dbi, &k, &v, 0);
    }

    mdb_txn_commit(txn);

    /* Get statistics */
    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);

    MDB_stat stat;
    mdb_stat(txn, dbi, &stat);

    printf("\n" COLOR_GREEN "Real Database Statistics:" COLOR_RESET "\n");
    printf("  Page size:     %u bytes\n", stat.ms_psize);
    printf("  Tree depth:    %u levels\n", stat.ms_depth);
    printf("  Branch pages:  %zu\n", stat.ms_branch_pages);
    printf("  Leaf pages:    %zu\n", stat.ms_leaf_pages);
    printf("  Entries:       %zu\n", stat.ms_entries);

    mdb_txn_abort(txn);
    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}

int main() {
    printf(COLOR_MAGENTA "\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     LMDB B+Tree Visualization Tool                         ║\n");
    printf("║                    Day 5 - B+Tree Structure               ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf(COLOR_RESET);

    /* Create and display sample tree */
    tree_node_t* root = create_sample_tree();

    printf("\n" COLOR_BOLD "Sample B+Tree Structure:" COLOR_RESET "\n");
    printf("\n");
    print_tree(root, 0);

    printf("\n" COLOR_CYAN "Press Enter to see ASCII visualization..." COLOR_RESET);
    getchar();

    visualize_tree_ascii();

    printf(COLOR_CYAN "\nPress Enter to see search operation..." COLOR_RESET);
    getchar();

    visualize_search("avocado");

    printf(COLOR_CYAN "\nPress Enter to see insert operation..." COLOR_RESET);
    getchar();

    visualize_insert("grape");

    printf(COLOR_CYAN "\nPress Enter to see delete operation..." COLOR_RESET);
    getchar();

    visualize_delete("cherry");

    printf(COLOR_CYAN "\nPress Enter to calculate tree statistics..." COLOR_RESET);
    getchar();

    tree_stats_t stats = {0};
    calculate_stats(root, &stats, 0);
    print_tree_stats(&stats);

    printf(COLOR_CYAN "\nPress Enter to create real database..." COLOR_RESET);
    getchar();

    create_real_database();

    printf(COLOR_CYAN "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║                    SUMMARY                                    ║\n");
    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    printf("\n" COLOR_YELLOW "B+Tree Properties:" COLOR_RESET "\n");
    printf("  1. " COLOR_GREEN "Balanced" COLOR_RESET " - All leaves at same depth\n");
    printf("  2. " COLOR_GREEN "Sorted" COLOR_RESET " - In-order traversal yields sorted keys\n");
    printf("  3. " COLOR_GREEN "Efficient" COLOR_RESET " - O(log n) search, insert, delete\n");
    printf("  4. " COLOR_GREEN "Range-friendly" COLOR_RESET " - Sequential scans are fast\n");

    printf("\n" COLOR_YELLOW "LMDB B+Tree Features:" COLOR_RESET "\n");
    printf("  • Page size matches system page (typically 4KB)\n");
    printf("  • Branch nodes contain only separators (no data)\n");
    printf("  • Leaf nodes contain key-value pairs\n");
    printf("  • Overflow pages for large values\n");
    printf("  • Copy-on-write for version management\n");

    printf(COLOR_GREEN "\n✓ tree_visualizer completed!\n" COLOR_RESET);
    printf("  Database created at: ./treeviz_db/\n");

    return 0;
}
