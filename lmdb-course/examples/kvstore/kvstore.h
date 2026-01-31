/**
 * kvstore.h - Key-Value Store with TTL Support
 *
 * Features:
 * - Simple key-value operations
 * - TTL (time-to-live) support
 * - Namespaces for key isolation
 * - Batch operations
 * - Atomic updates
 */

#ifndef KVSTORE_H
#define KVSTORE_H

#include <lmdb.h>
#include <time.h>
#include <stdint.h>

#define MAX_KEY_SIZE 255
#define MAX_VALUE_SIZE 4096
#define MAX_NAMESPACE_SIZE 64
#define MAX_TTL 86400  // 24 hours in seconds

// Database names
#define DB_DATA "data"
#define DB_INDEX "index"
#define DB_EXPIRY "expiry"

// Value metadata
typedef struct {
    char value[MAX_VALUE_SIZE];
    uint64_t expires_at;  // 0 means no expiration
    uint32_t flags;
    uint32_t value_size;
} kv_value_t;

// Key-Value store handle
typedef struct {
    MDB_env *env;
    char path[256];
    size_t map_size;
    int max_readers;
} kvstore_t;

// Statistics
typedef struct {
    uint64_t total_keys;
    uint64_t expired_keys;
    uint64_t namespaces;
    uint64_t total_size;
} kv_stats_t;

// Batch operation context
typedef struct {
    kvstore_t *store;
    MDB_txn *txn;
    int operation_count;
} kv_batch_t;

// Initialize key-value store
int kvstore_init(kvstore_t *store, const char *path, size_t map_size);

// Close key-value store
void kvstore_close(kvstore_t *store);

// Basic operations
int kvstore_put(kvstore_t *store, const char *key, const void *value,
                size_t size, int ttl);
int kvstore_get(kvstore_t *store, const char *key, void *value,
                size_t *size);
int kvstore_delete(kvstore_t *store, const char *key);
int kvstore_exists(kvstore_t *store, const char *key);

// TTL operations
int kvstore_set_ttl(kvstore_t *store, const char *key, int ttl);
int kvstore_get_ttl(kvstore_t *store, const char *key, int *ttl);
int kvstore_touch(kvstore_t *store, const char *key);

// Namespace operations
int kvstore_ns_put(kvstore_t *store, const char *ns, const char *key,
                   const void *value, size_t size, int ttl);
int kvstore_ns_get(kvstore_t *store, const char *ns, const char *key,
                   void *value, size_t *size);
int kvstore_ns_delete(kvstore_t *store, const char *ns, const char *key);

// Batch operations
int kvstore_batch_begin(kvstore_t *store, kv_batch_t *batch);
int kvstore_batch_put(kv_batch_t *batch, const char *key,
                       const void *value, size_t size, int ttl);
int kvstore_batch_delete(kv_batch_t *batch, const char *key);
int kvstore_batch_commit(kv_batch_t *batch);
int kvstore_batch_abort(kv_batch_t *batch);

// Atomic operations
int kvstore_increment(kvstore_t *store, const char *key, int64_t delta,
                      int64_t *result);
int kvstore_compare_and_set(kvstore_t *store, const char *key,
                            const void *expected, const void *new_value,
                            size_t size);

// Cleanup operations
int kvstore_cleanup_expired(kvstore_t *store, uint64_t *cleaned_count);

// Statistics
int kvstore_get_stats(kvstore_t *store, kv_stats_t *stats);

// Utility operations
int kvstore_list_keys(kvstore_t *store, const char *prefix,
                      void (*callback)(const char*, void*), void *arg);
int kvstore_clear(kvstore_t *store);

#endif // KVSTORE_H
