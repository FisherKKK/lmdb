/**
 * kvstore.c - Key-Value Store Implementation
 */

#include "kvstore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static pthread_mutex_t kv_mutex = PTHREAD_MUTEX_INITIALIZER;

// Initialize key-value store
int kvstore_init(kvstore_t *store, const char *path, size_t map_size) {
    int rc;

    if (!store || !path) return -1;

    memset(store, 0, sizeof(kvstore_t));
    strncpy(store->path, path, sizeof(store->path) - 1);
    store->map_size = map_size;
    store->max_readers = 64;

    rc = mdb_env_create(&store->env);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_create failed: %s\n", mdb_strerror(rc));
        return rc;
    }

    rc = mdb_env_set_mapsize(store->env, map_size);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_set_mapsize failed: %s\n", mdb_strerror(rc));
        mdb_env_close(store->env);
        return rc;
    }

    rc = mdb_env_set_maxdbs(store->env, 10);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_set_maxdbs failed: %s\n", mdb_strerror(rc));
        mdb_env_close(store->env);
        return rc;
    }

    rc = mdb_env_set_maxreaders(store->env, store->max_readers);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_set_maxreaders failed: %s\n", mdb_strerror(rc));
        mdb_env_close(store->env);
        return rc;
    }

    rc = mdb_env_open(store->env, path, 0, 0664);
    if (rc != 0) {
        fprintf(stderr, "mdb_env_open failed: %s\n", mdb_strerror(rc));
        mdb_env_close(store->env);
        return rc;
    }

    printf("KeyValueStore initialized at %s (map size: %zu MB)\n",
           path, map_size / (1024 * 1024));

    return 0;
}

// Close key-value store
void kvstore_close(kvstore_t *store) {
    if (store && store->env) {
        mdb_env_close(store->env);
        store->env = NULL;
        printf("KeyValueStore closed\n");
    }
}

// Internal: Check if key has expired
static int is_expired(kv_value_t *kv) {
    if (kv->expires_at == 0) return 0;
    return time(NULL) >= kv->expires_at;
}

// Basic PUT operation
int kvstore_put(kvstore_t *store, const char *key, const void *value,
                size_t size, int ttl) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val mkey, mdata;
    kv_value_t kv;
    int rc;

    if (!store || !key || !value) return -1;
    if (size > MAX_VALUE_SIZE) return -1;

    pthread_mutex_lock(&kv_mutex);

    rc = mdb_txn_begin(store->env, NULL, 0, &txn);
    if (rc != 0) {
        pthread_mutex_unlock(&kv_mutex);
        return rc;
    }

    rc = mdb_dbi_open(txn, DB_DATA, MDB_CREATE, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        pthread_mutex_unlock(&kv_mutex);
        return rc;
    }

    // Prepare value with metadata
    memset(&kv, 0, sizeof(kv));
    memcpy(kv.value, value, size);
    kv.value_size = size;

    if (ttl > 0) {
        kv.expires_at = time(NULL) + ttl;
    } else {
        kv.expires_at = 0;
    }

    mkey.mv_data = (void*)key;
    mkey.mv_size = strlen(key) + 1;
    mdata.mv_data = &kv;
    mdata.mv_size = sizeof(kv);

    rc = mdb_put(txn, dbi, &mkey, &mdata, 0);

    pthread_mutex_unlock(&kv_mutex);
    return mdb_txn_commit(txn);
}

// Basic GET operation
int kvstore_get(kvstore_t *store, const char *key, void *value,
                size_t *size) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val mkey, mdata;
    kv_value_t *kv;
    int rc;

    if (!store || !key || !value) return -1;

    rc = mdb_txn_begin(store->env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) return rc;

    rc = mdb_dbi_open(txn, DB_DATA, 0, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    mkey.mv_data = (void*)key;
    mkey.mv_size = strlen(key) + 1;

    rc = mdb_get(txn, dbi, &mkey, &mdata);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    kv = (kv_value_t*)mdata.mv_data;

    // Check expiration
    if (is_expired(kv)) {
        mdb_txn_abort(txn);
        return MDB_NOTFOUND;
    }

    memcpy(value, kv->value, kv->value_size);
    if (size) *size = kv->value_size;

    mdb_txn_abort(txn);
    return 0;
}

// Basic DELETE operation
int kvstore_delete(kvstore_t *store, const char *key) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val mkey;
    int rc;

    if (!store || !key) return -1;

    pthread_mutex_lock(&kv_mutex);

    rc = mdb_txn_begin(store->env, NULL, 0, &txn);
    if (rc != 0) {
        pthread_mutex_unlock(&kv_mutex);
        return rc;
    }

    rc = mdb_dbi_open(txn, DB_DATA, 0, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        pthread_mutex_unlock(&kv_mutex);
        return rc;
    }

    mkey.mv_data = (void*)key;
    mkey.mv_size = strlen(key) + 1;

    rc = mdb_del(txn, dbi, &mkey, NULL);

    pthread_mutex_unlock(&kv_mutex);

    if (rc == MDB_NOTFOUND) {
        mdb_txn_abort(txn);
        return -1;
    }

    return mdb_txn_commit(txn);
}

// Check if key exists
int kvstore_exists(kvstore_t *store, const char *key) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val mkey, mdata;
    kv_value_t *kv;
    int rc;

    if (!store || !key) return -1;

    rc = mdb_txn_begin(store->env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) return 0;

    rc = mdb_dbi_open(txn, DB_DATA, 0, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return 0;
    }

    mkey.mv_data = (void*)key;
    mkey.mv_size = strlen(key) + 1;

    rc = mdb_get(txn, dbi, &mkey, &mdata);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return 0;
    }

    kv = (kv_value_t*)mdata.mv_data;

    // Check expiration
    int exists = !is_expired(kv);
    mdb_txn_abort(txn);

    return exists;
}

// Set TTL for existing key
int kvstore_set_ttl(kvstore_t *store, const char *key, int ttl) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val mkey, mdata;
    kv_value_t *kv;
    int rc;

    if (!store || !key) return -1;

    pthread_mutex_lock(&kv_mutex);

    rc = mdb_txn_begin(store->env, NULL, 0, &txn);
    if (rc != 0) {
        pthread_mutex_unlock(&kv_mutex);
        return rc;
    }

    rc = mdb_dbi_open(txn, DB_DATA, 0, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        pthread_mutex_unlock(&kv_mutex);
        return rc;
    }

    mkey.mv_data = (void*)key;
    mkey.mv_size = strlen(key) + 1;

    rc = mdb_get(txn, dbi, &mkey, &mdata);
    if (rc != 0) {
        mdb_txn_abort(txn);
        pthread_mutex_unlock(&kv_mutex);
        return rc;
    }

    kv = (kv_value_t*)mdata.mv_data;

    if (ttl > 0) {
        kv->expires_at = time(NULL) + ttl;
    } else {
        kv->expires_at = 0;
    }

    rc = mdb_put(txn, dbi, &mkey, &mdata, 0);

    pthread_mutex_unlock(&kv_mutex);
    return mdb_txn_commit(txn);
}

// Get TTL for key
int kvstore_get_ttl(kvstore_t *store, const char *key, int *ttl) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val mkey, mdata;
    kv_value_t *kv;
    int rc;

    if (!store || !key || !ttl) return -1;

    rc = mdb_txn_begin(store->env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) return rc;

    rc = mdb_dbi_open(txn, DB_DATA, 0, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    mkey.mv_data = (void*)key;
    mkey.mv_size = strlen(key) + 1;

    rc = mdb_get(txn, dbi, &mkey, &mdata);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    kv = (kv_value_t*)mdata.mv_data;

    if (kv->expires_at == 0) {
        *ttl = -1;  // No expiration
    } else {
        time_t now = time(NULL);
        if (kv->expires_at <= now) {
            *ttl = 0;  // Expired
        } else {
            *ttl = (int)(kv->expires_at - now);
        }
    }

    mdb_txn_abort(txn);
    return 0;
}

// Touch key (update access time, extend TTL)
int kvstore_touch(kvstore_t *store, const char *key) {
    int ttl;
    int rc = kvstore_get_ttl(store, key, &ttl);
    if (rc != 0 || ttl == 0) return -1;

    // Extend TTL by same amount (or to default if no TTL was set)
    if (ttl > 0) {
        return kvstore_set_ttl(store, key, ttl);
    }
    return 0;
}

// Namespace PUT operation
int kvstore_ns_put(kvstore_t *store, const char *ns, const char *key,
                   const void *value, size_t size, int ttl) {
    char ns_key[MAX_KEY_SIZE * 2];

    if (!ns || !key) return -1;

    snprintf(ns_key, sizeof(ns_key), "%s:%s", ns, key);
    return kvstore_put(store, ns_key, value, size, ttl);
}

// Namespace GET operation
int kvstore_ns_get(kvstore_t *store, const char *ns, const char *key,
                   void *value, size_t *size) {
    char ns_key[MAX_KEY_SIZE * 2];

    if (!ns || !key) return -1;

    snprintf(ns_key, sizeof(ns_key), "%s:%s", ns, key);
    return kvstore_get(store, ns_key, value, size);
}

// Namespace DELETE operation
int kvstore_ns_delete(kvstore_t *store, const char *ns, const char *key) {
    char ns_key[MAX_KEY_SIZE * 2];

    if (!ns || !key) return -1;

    snprintf(ns_key, sizeof(ns_key), "%s:%s", ns, key);
    return kvstore_delete(store, ns_key);
}

// Begin batch operation
int kvstore_batch_begin(kvstore_t *store, kv_batch_t *batch) {
    int rc;

    if (!store || !batch) return -1;

    pthread_mutex_lock(&kv_mutex);

    rc = mdb_txn_begin(store->env, NULL, 0, &batch->txn);
    if (rc != 0) {
        pthread_mutex_unlock(&kv_mutex);
        return rc;
    }

    batch->store = store;
    batch->operation_count = 0;

    return 0;
}

// Batch PUT
int kvstore_batch_put(kv_batch_t *batch, const char *key,
                       const void *value, size_t size, int ttl) {
    MDB_dbi dbi;
    MDB_val mkey, mdata;
    kv_value_t kv;
    int rc;

    if (!batch || !key || !value) return -1;

    rc = mdb_dbi_open(batch->txn, DB_DATA, MDB_CREATE, &dbi);
    if (rc != 0) return rc;

    memset(&kv, 0, sizeof(kv));
    memcpy(kv.value, value, size);
    kv.value_size = size;

    if (ttl > 0) {
        kv.expires_at = time(NULL) + ttl;
    }

    mkey.mv_data = (void*)key;
    mkey.mv_size = strlen(key) + 1;
    mdata.mv_data = &kv;
    mdata.mv_size = sizeof(kv);

    rc = mdb_put(batch->txn, dbi, &mkey, &mdata, 0);
    if (rc == 0) {
        batch->operation_count++;
    }

    return rc;
}

// Batch DELETE
int kvstore_batch_delete(kv_batch_t *batch, const char *key) {
    MDB_dbi dbi;
    MDB_val mkey;
    int rc;

    if (!batch || !key) return -1;

    rc = mdb_dbi_open(batch->txn, DB_DATA, 0, &dbi);
    if (rc != 0) return rc;

    mkey.mv_data = (void*)key;
    mkey.mv_size = strlen(key) + 1;

    rc = mdb_del(batch->txn, dbi, &mkey, NULL);
    if (rc == 0) {
        batch->operation_count++;
    }

    return rc;
}

// Commit batch operation
int kvstore_batch_commit(kv_batch_t *batch) {
    int rc;

    if (!batch) return -1;

    rc = mdb_txn_commit(batch->txn);
    pthread_mutex_unlock(&kv_mutex);

    printf("Batch committed: %d operations\n", batch->operation_count);

    return rc;
}

// Abort batch operation
int kvstore_batch_abort(kv_batch_t *batch) {
    if (!batch) return -1;

    mdb_txn_abort(batch->txn);
    pthread_mutex_unlock(&kv_mutex);

    printf("Batch aborted: %d operations\n", batch->operation_count);

    return 0;
}

// Atomic increment operation
int kvstore_increment(kvstore_t *store, const char *key, int64_t delta,
                      int64_t *result) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val mkey, mdata;
    kv_value_t *kv;
    int64_t current_value;
    int rc;

    if (!store || !key) return -1;

    pthread_mutex_lock(&kv_mutex);

    rc = mdb_txn_begin(store->env, NULL, 0, &txn);
    if (rc != 0) {
        pthread_mutex_unlock(&kv_mutex);
        return rc;
    }

    rc = mdb_dbi_open(txn, DB_DATA, MDB_CREATE, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        pthread_mutex_unlock(&kv_mutex);
        return rc;
    }

    mkey.mv_data = (void*)key;
    mkey.mv_size = strlen(key) + 1;

    rc = mdb_get(txn, dbi, &mkey, &mdata);

    if (rc == MDB_NOTFOUND) {
        // Key doesn't exist, start from 0
        current_value = 0;
    } else if (rc == 0) {
        kv = (kv_value_t*)mdata.mv_data;
        if (is_expired(kv)) {
            current_value = 0;
        } else {
            memcpy(&current_value, kv->value, sizeof(int64_t));
        }
    } else {
        mdb_txn_abort(txn);
        pthread_mutex_unlock(&kv_mutex);
        return rc;
    }

    current_value += delta;

    kv_value_t new_kv;
    memset(&new_kv, 0, sizeof(new_kv));
    memcpy(new_kv.value, &current_value, sizeof(int64_t));
    new_kv.value_size = sizeof(int64_t);
    new_kv.expires_at = 0;

    mdata.mv_data = &new_kv;
    mdata.mv_size = sizeof(new_kv);

    rc = mdb_put(txn, dbi, &mkey, &mdata, 0);

    if (result) *result = current_value;

    pthread_mutex_unlock(&kv_mutex);
    return mdb_txn_commit(txn);
}

// Cleanup expired keys
int kvstore_cleanup_expired(kvstore_t *store, uint64_t *cleaned_count) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_cursor *cursor;
    MDB_val key, data;
    uint64_t count = 0;
    int rc;

    if (!store) return -1;

    pthread_mutex_lock(&kv_mutex);

    rc = mdb_txn_begin(store->env, NULL, 0, &txn);
    if (rc != 0) {
        pthread_mutex_unlock(&kv_mutex);
        return rc;
    }

    rc = mdb_dbi_open(txn, DB_DATA, 0, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        pthread_mutex_unlock(&kv_mutex);
        return rc;
    }

    rc = mdb_cursor_open(txn, dbi, &cursor);
    if (rc != 0) {
        mdb_txn_abort(txn);
        pthread_mutex_unlock(&kv_mutex);
        return rc;
    }

    rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    while (rc == 0) {
        kv_value_t *kv = (kv_value_t*)data.mv_data;

        if (is_expired(kv)) {
            mdb_cursor_del(cursor, 0);
            count++;
        }

        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    pthread_mutex_unlock(&kv_mutex);

    rc = mdb_txn_commit(txn);

    if (cleaned_count) *cleaned_count = count;

    printf("Cleaned up %lu expired keys\n", count);

    return rc;
}

// Get statistics
int kvstore_get_stats(kvstore_t *store, kv_stats_t *stats) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_stat st;
    int rc;

    if (!store || !stats) return -1;

    memset(stats, 0, sizeof(*stats));

    rc = mdb_txn_begin(store->env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) return rc;

    rc = mdb_dbi_open(txn, DB_DATA, 0, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    mdb_stat(txn, dbi, &st);
    stats->total_keys = st.ms_entries;
    stats->total_size = st.ms_psize * st.ms_depth;

    mdb_txn_abort(txn);
    return 0;
}

// List keys with prefix
int kvstore_list_keys(kvstore_t *store, const char *prefix,
                      void (*callback)(const char*, void*), void *arg) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_cursor *cursor;
    MDB_val key, data;
    int rc;

    if (!store || !callback) return -1;

    rc = mdb_txn_begin(store->env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) return rc;

    rc = mdb_dbi_open(txn, DB_DATA, 0, &dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    rc = mdb_cursor_open(txn, dbi, &cursor);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return rc;
    }

    if (prefix && strlen(prefix) > 0) {
        MDB_val prefix_key;
        prefix_key.mv_data = (void*)prefix;
        prefix_key.mv_size = strlen(prefix);

        rc = mdb_cursor_get(cursor, &key, &data, MDB_SET_RANGE);
    } else {
        rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    }

    while (rc == 0) {
        char *key_str = (char*)key.mv_data;

        // Check prefix match
        if (prefix && strlen(prefix) > 0 &&
            strncmp(key_str, prefix, strlen(prefix)) != 0) {
            break;
        }

        callback(key_str, arg);

        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);

    return 0;
}

// Clear all data
int kvstore_clear(kvstore_t *store) {
    int rc;

    if (!store) return -1;

    pthread_mutex_lock(&kv_mutex);

    // Close and reopen environment to clear all data
    mdb_env_close(store->env);

    rc = mdb_env_create(&store->env);
    if (rc != 0) {
        pthread_mutex_unlock(&kv_mutex);
        return rc;
    }

    rc = mdb_env_set_mapsize(store->env, store->map_size);
    if (rc != 0) {
        pthread_mutex_unlock(&kv_mutex);
        return rc;
    }

    rc = mdb_env_set_maxdbs(store->env, 10);
    if (rc != 0) {
        pthread_mutex_unlock(&kv_mutex);
        return rc;
    }

    rc = mdb_env_set_maxreaders(store->env, store->max_readers);
    if (rc != 0) {
        pthread_mutex_unlock(&kv_mutex);
        return rc;
    }

    rc = mdb_env_open(store->env, store->path, 0, 0664);

    pthread_mutex_unlock(&kv_mutex);

    return rc;
}
