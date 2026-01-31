/**
 * cache_simulator.c - LMDB 页面缓存模拟器
 *
 * 模拟和分析 LMDB 的页面缓存行为：
 * - 模拟操作系统页面缓存
 * - 分析缓存命中率
 * - 预测工作集大小
 * - 优化建议
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <lmdb.h>

#define CACHE_SIZE 1000  // 模拟缓存页面数

typedef struct {
    unsigned int page_no;
    int access_count;
    time_t last_access;
} cache_entry_t;

typedef struct {
    cache_entry_t entries[CACHE_SIZE];
    int size;
    long total_accesses;
    long cache_hits;
    long cache_misses;
} cache_sim_t;

// 初始化缓存模拟器
void cache_init(cache_sim_t *cache) {
    memset(cache, 0, sizeof(cache_sim_t));
    cache->size = 0;
}

// 模拟页面访问
void cache_access(cache_sim_t *cache, unsigned int page_no) {
    cache->total_accesses++;

    // 查找页面是否在缓存中
    for (int i = 0; i < cache->size; i++) {
        if (cache->entries[i].page_no == page_no) {
            cache->entries[i].access_count++;
            cache->entries[i].last_access = time(NULL);
            cache->cache_hits++;
            return;
        }
    }

    // 缓存未命中
    cache->cache_misses++;

    // 如果缓存未满，添加新条目
    if (cache->size < CACHE_SIZE) {
        cache->entries[cache->size].page_no = page_no;
        cache->entries[cache->size].access_count = 1;
        cache->entries[cache->size].last_access = time(NULL);
        cache->size++;
    } else {
        // LRU 替换：找到最久未访问的条目
        int lru_idx = 0;
        time_t oldest = cache->entries[0].last_access;

        for (int i = 1; i < cache->size; i++) {
            if (cache->entries[i].last_access < oldest) {
                oldest = cache->entries[i].last_access;
                lru_idx = i;
            }
        }

        // 替换
        cache->entries[lru_idx].page_no = page_no;
        cache->entries[lru_idx].access_count = 1;
        cache->entries[lru_idx].last_access = time(NULL);
    }
}

// 打印缓存统计
void print_cache_stats(cache_sim_t *cache) {
    double hit_rate = 0.0;
    double miss_rate = 0.0;

    if (cache->total_accesses > 0) {
        hit_rate = 100.0 * cache->cache_hits / cache->total_accesses;
        miss_rate = 100.0 * cache->cache_misses / cache->total_accesses;
    }

    printf("\n========== Cache Simulation Results ==========\n");
    printf("Cache Size:       %d pages\n", CACHE_SIZE);
    printf("Total Accesses:   %ld\n", cache->total_accesses);
    printf("Cache Hits:       %ld (%.2f%%)\n", cache->cache_hits, hit_rate);
    printf("Cache Misses:     %ld (%.2f%%)\n", cache->cache_misses, miss_rate);

    // 找出最常访问的页面
    printf("\nTop 10 Most Accessed Pages:\n");
    for (int i = 0; i < 10 && i < cache->size; i++) {
        int max_idx = 0;
        int max_count = cache->entries[0].access_count;

        for (int j = 1; j < cache->size; j++) {
            if (cache->entries[j].access_count > max_count) {
                max_count = cache->entries[j].access_count;
                max_idx = j;
            }
        }

        printf("  Page %6u: %d accesses\n",
               cache->entries[max_idx].page_no, max_count);
        cache->entries[max_idx].access_count = -1;  // 标记为已处理
    }
}

// 模拟顺序扫描访问模式
void simulate_sequential_scan(cache_sim_t *cache, unsigned int start_page, int num_pages) {
    printf("\nSimulating sequential scan of %d pages starting from page %u...\n",
           num_pages, start_page);

    for (int i = 0; i < num_pages; i++) {
        cache_access(cache, start_page + i);
    }
}

// 模拟随机访问模式
void simulate_random_access(cache_sim_t *cache, unsigned int max_page, int num_accesses) {
    printf("\nSimulating random access (%d accesses, max page %u)...\n",
           num_accesses, max_page);

    srand(time(NULL));
    for (int i = 0; i < num_accesses; i++) {
        unsigned int page = rand() % max_page;
        cache_access(cache, page);
    }
}

// 模拟局部性访问模式
void simulate_localized_access(cache_sim_t *cache, unsigned int base_page, int num_pages) {
    printf("\nSimulating localized access around page %u (%d accesses)...\n",
           base_page, num_pages);

    srand(time(NULL));
    for (int i = 0; i < num_pages; i++) {
        // 大部分访问集中在 base_page 附近
        unsigned int page;
        if (rand() % 100 < 80) {
            page = base_page + (rand() % 20) - 10;
        } else {
            page = rand() % 1000;
        }
        cache_access(cache, page);
    }
}

// 工作集分析
void analyze_working_set(MDB_env *env) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_cursor *cursor;
    MDB_val key, data;
    int rc;
    int page_count = 0;

    printf("\n========== Working Set Analysis ==========\n");

    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) {
        fprintf(stderr, "Cannot begin transaction\n");
        return;
    }

    rc = mdb_dbi_open(txn, NULL, 0, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return;
    }

    rc = mdb_cursor_open(txn, dbi, &cursor);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return;
    }

    // 收集访问的页面（简化模拟）
    rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    while (rc == 0 && page_count < 10000) {
        // 模拟：每个键值对可能在不同页面
        page_count++;
        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);

    printf("Total entries: %d\n", page_count);
    printf("Estimated working set: ");
    if (page_count < 100) {
        printf("Small (< 100 pages)\n");
    } else if (page_count < 1000) {
        printf("Medium (100-1000 pages)\n");
    } else {
        printf("Large (> 1000 pages)\n");
    }
}

int main(int argc, char **argv) {
    MDB_env *env;
    char *db_path = "./testdb";
    int rc;

    if (argc > 1) {
        db_path = argv[1];
    }

    printf("========================================\n");
    printf("    LMDB Cache Simulator v1.0         \n");
    printf("========================================\n");
    printf("\nDatabase: %s\n", db_path);

    rc = mdb_env_create(&env);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_create failed: %s\n", mdb_strerror(rc));
        return 1;
    }

    rc = mdb_env_open(env, db_path, MDB_RDONLY, 0664);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_open failed: %s\n", mdb_strerror(rc));
        mdb_env_close(env);
        return 1;
    }

    // 分析工作集
    analyze_working_set(env);

    // 运行不同的访问模式模拟
    cache_sim_t cache;
    cache_init(&cache);

    simulate_sequential_scan(&cache, 0, 100);
    simulate_random_access(&cache, 1000, 500);
    simulate_localized_access(&cache, 100, 500);

    // 打印结果
    print_cache_stats(&cache);

    // 性能建议
    printf("\n========== Performance Recommendations ==========\n");
    if (cache.cache_hits + cache.cache_misses > 0) {
        double hit_rate = 100.0 * cache.cache_hits / cache.total_accesses;
        if (hit_rate > 80) {
            printf("✓ Cache hit rate is good (%.1f%%)\n", hit_rate);
            printf("  Current cache size is appropriate\n");
        } else if (hit_rate > 50) {
            printf("⚠ Cache hit rate is moderate (%.1f%%)\n", hit_rate);
            printf("  Consider increasing cache size or optimizing access patterns\n");
        } else {
            printf("✗ Cache hit rate is low (%.1f%%)\n", hit_rate);
            printf("  Recommendations:\n");
            printf("    - Increase database map size\n");
            printf("    - Optimize access patterns for better locality\n");
            printf("    - Use batch operations when possible\n");
        }
    }

    mdb_env_close(env);

    printf("\n========================================\n");
    printf("       Simulation Complete!             \n");
    printf("========================================\n");

    return 0;
}
