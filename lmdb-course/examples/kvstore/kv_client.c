/**
 * kv_client.c - Key-Value Store Command Line Client
 *
 * Command-line interface for the key-value store
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "kvstore.h"

void print_stats(kvstore_t *store) {
    kv_stats_t stats;

    if (kvstore_get_stats(store, &stats) == 0) {
        printf("\n========== Key-Value Store Statistics ==========\n");
        printf("Total Keys:     %lu\n", stats.total_keys);
        printf("Total Size:     %lu bytes\n", stats.total_size);
        printf("Namespaces:     %lu\n", stats.namespaces);
        printf("Expired Keys:   %lu\n", stats.expired_keys);
        printf("================================================\n\n");
    }
}

void key_callback(const char *key, void *arg) {
    printf("  - %s\n", key);
}

int main(int argc, char **argv) {
    kvstore_t store;
    char *db_path = "./kvstore_db";
    int rc;

    if (argc < 2) {
        printf("Usage: %s <command> [options]\n\n", argv[0]);
        printf("Commands:\n");
        printf("  set <key> <value> [ttl]              Set a key-value pair\n");
        printf("  get <key>                             Get a value by key\n");
        printf("  delete <key>                          Delete a key\n");
        printf("  exists <key>                          Check if key exists\n");
        printf("  ttl <key> [seconds]                   Get/Set TTL for key\n");
        printf("  touch <key>                           Refresh key TTL\n");
        printf("  incr <key> [delta]                    Increment counter\n");
        printf("  ns-set <ns> <key> <value> [ttl]       Set in namespace\n");
        printf("  ns-get <ns> <key>                     Get from namespace\n");
        printf("  list [prefix]                         List all keys\n");
        printf("  cleanup                               Remove expired keys\n");
        printf("  stats                                 Show statistics\n");
        printf("  clear                                 Clear all data\n");
        printf("\nExamples:\n");
        printf("  %s set user:1 \"John Doe\" 3600\n", argv[0]);
        printf("  %s get user:1\n", argv[0]);
        printf("  %s incr counter 5\n", argv[0]);
        printf("  %s list user:\n", argv[0]);
        return 1;
    }

    char *command = argv[1];

    // Initialize store
    rc = kvstore_init(&store, db_path, 1024 * 1024 * 100);
    if (rc != 0) {
        fprintf(stderr, "Failed to initialize key-value store: %d\n", rc);
        return 1;
    }

    // Process commands
    if (strcmp(command, "set") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s set <key> <value> [ttl]\n", argv[0]);
            kvstore_close(&store);
            return 1;
        }

        char *key = argv[2];
        char *value = argv[3];
        int ttl = (argc >= 5) ? atoi(argv[4]) : 0;

        rc = kvstore_put(&store, key, value, strlen(value), ttl);
        if (rc == 0) {
            printf("Key set: %s", key);
            if (ttl > 0) {
                printf(" (TTL: %d seconds)\n", ttl);
            } else {
                printf("\n");
            }
        } else {
            fprintf(stderr, "Failed to set key: %s\n", mdb_strerror(rc));
        }

    } else if (strcmp(command, "get") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s get <key>\n", argv[0]);
            kvstore_close(&store);
            return 1;
        }

        char *key = argv[2];
        char value[MAX_VALUE_SIZE];
        size_t size;

        rc = kvstore_get(&store, key, value, &size);
        if (rc == 0) {
            printf("Value: %.*s\n", (int)size, value);

            int ttl;
            if (kvstore_get_ttl(&store, key, &ttl) == 0) {
                if (ttl == -1) {
                    printf("TTL: No expiration\n");
                } else if (ttl > 0) {
                    printf("TTL: %d seconds remaining\n", ttl);
                } else {
                    printf("TTL: Expired\n");
                }
            }
        } else {
            fprintf(stderr, "Key not found: %s\n", key);
        }

    } else if (strcmp(command, "delete") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s delete <key>\n", argv[0]);
            kvstore_close(&store);
            return 1;
        }

        char *key = argv[2];
        rc = kvstore_delete(&store, key);
        if (rc == 0) {
            printf("Key deleted: %s\n", key);
        } else {
            fprintf(stderr, "Failed to delete key: %s\n", key);
        }

    } else if (strcmp(command, "exists") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s exists <key>\n", argv[0]);
            kvstore_close(&store);
            return 1;
        }

        char *key = argv[2];
        rc = kvstore_exists(&store, key);
        printf("Key '%s' %s\n", key, rc ? "exists" : "does not exist");

    } else if (strcmp(command, "ttl") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s ttl <key> [seconds]\n", argv[0]);
            kvstore_close(&store);
            return 1;
        }

        char *key = argv[2];

        if (argc >= 4) {
            int ttl = atoi(argv[3]);
            rc = kvstore_set_ttl(&store, key, ttl);
            if (rc == 0) {
                printf("TTL set for key '%s': %d seconds\n", key, ttl);
            } else {
                fprintf(stderr, "Failed to set TTL\n");
            }
        } else {
            int ttl;
            rc = kvstore_get_ttl(&store, key, &ttl);
            if (rc == 0) {
                if (ttl == -1) {
                    printf("Key '%s' has no expiration\n", key);
                } else if (ttl > 0) {
                    printf("Key '%s' TTL: %d seconds remaining\n", key, ttl);
                } else {
                    printf("Key '%s' has expired\n", key);
                }
            } else {
                fprintf(stderr, "Failed to get TTL\n");
            }
        }

    } else if (strcmp(command, "touch") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s touch <key>\n", argv[0]);
            kvstore_close(&store);
            return 1;
        }

        char *key = argv[2];
        rc = kvstore_touch(&store, key);
        if (rc == 0) {
            printf("Key refreshed: %s\n", key);
        } else {
            fprintf(stderr, "Failed to refresh key\n");
        }

    } else if (strcmp(command, "incr") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s incr <key> [delta]\n", argv[0]);
            kvstore_close(&store);
            return 1;
        }

        char *key = argv[2];
        int64_t delta = (argc >= 4) ? atoll(argv[3]) : 1;
        int64_t result;

        rc = kvstore_increment(&store, key, delta, &result);
        if (rc == 0) {
            printf("Counter incremented: %s = %ld\n", key, result);
        } else {
            fprintf(stderr, "Failed to increment counter\n");
        }

    } else if (strcmp(command, "ns-set") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Usage: %s ns-set <ns> <key> <value> [ttl]\n", argv[0]);
            kvstore_close(&store);
            return 1;
        }

        char *ns = argv[2];
        char *key = argv[3];
        char *value = argv[4];
        int ttl = (argc >= 6) ? atoi(argv[5]) : 0;

        rc = kvstore_ns_put(&store, ns, key, value, strlen(value), ttl);
        if (rc == 0) {
            printf("Key set in namespace '%s': %s\n", ns, key);
        } else {
            fprintf(stderr, "Failed to set key\n");
        }

    } else if (strcmp(command, "ns-get") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s ns-get <ns> <key>\n", argv[0]);
            kvstore_close(&store);
            return 1;
        }

        char *ns = argv[2];
        char *key = argv[3];
        char value[MAX_VALUE_SIZE];
        size_t size;

        rc = kvstore_ns_get(&store, ns, key, value, &size);
        if (rc == 0) {
            printf("Value from namespace '%s': %.*s\n", ns, (int)size, value);
        } else {
            fprintf(stderr, "Key not found: %s:%s\n", ns, key);
        }

    } else if (strcmp(command, "list") == 0) {
        char *prefix = (argc >= 3) ? argv[2] : NULL;

        printf("Listing keys%s%s:\n",
               prefix ? " with prefix '" : "",
               prefix ? prefix : "");

        kvstore_list_keys(&store, prefix, key_callback, NULL);
        printf("\n");

    } else if (strcmp(command, "cleanup") == 0) {
        uint64_t cleaned;

        rc = kvstore_cleanup_expired(&store, &cleaned);
        if (rc == 0) {
            printf("Cleanup completed\n");
        } else {
            fprintf(stderr, "Cleanup failed: %s\n", mdb_strerror(rc));
        }

    } else if (strcmp(command, "stats") == 0) {
        print_stats(&store);

    } else if (strcmp(command, "clear") == 0) {
        printf("This will delete ALL data. Are you sure? (yes/no): ");
        char confirm[10];
        if (fgets(confirm, sizeof(confirm), stdin) &&
            strncmp(confirm, "yes", 3) == 0) {
            rc = kvstore_clear(&store);
            if (rc == 0) {
                printf("All data cleared\n");
            } else {
                fprintf(stderr, "Failed to clear data\n");
            }
        } else {
            printf("Clear cancelled\n");
        }

    } else {
        fprintf(stderr, "Unknown command: %s\n", command);
        kvstore_close(&store);
        return 1;
    }

    kvstore_close(&store);
    return 0;
}
