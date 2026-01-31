/**
 * db_tools.c - LMDB 数据库实用工具集
 *
 * 提供多个实用的数据库操作：
 * - 数据导出
 * - 数据导入
 * - 数据库备份
 * - 数据库比较
 * - 数据库统计
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <lmdb.h>

// 导出数据库到文本文件
int export_db(MDB_env *env, const char *filename) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_cursor *cursor;
    MDB_val key, data;
    FILE *fp;
    int rc;
    int count = 0;

    printf("Exporting database to %s...\n", filename);

    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) {
        fprintf(stderr, "mdb_txn_begin failed: %s\n", mdb_strerror(rc));
        return -1;
    }

    rc = mdb_dbi_open(txn, NULL, 0, &dbi);
    if (rc != 0) {
        fprintf(stderr, "mdb_dbi_open failed: %s\n", mdb_strerror(rc));
        mdb_txn_abort(txn);
        return -1;
    }

    fp = fopen(filename, "w");
    if (fp == NULL) {
        perror("fopen");
        mdb_txn_abort(txn);
        return -1;
    }

    rc = mdb_cursor_open(txn, dbi, &cursor);
    if (rc != 0) {
        fprintf(stderr, "mdb_cursor_open failed: %s\n", mdb_strerror(rc));
        fclose(fp);
        mdb_txn_abort(txn);
        return -1;
    }

    // 写入文件头
    fprintf(fp, "# LMDB Database Export\n");
    fprintf(fp, "# Exported at: %s", ctime(&(time_t){time(NULL)}));
    fprintf(fp, "# Format: key_size|key_data|value_size|value_data\n");
    fprintf(fp, "# ----------------------------------------\n");

    rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    while (rc == 0) {
        // 写入键
        fprintf(fp, "%zu|", key.mv_size);
        for (size_t i = 0; i < key.mv_size; i++) {
            fprintf(fp, "%02x", ((unsigned char *)key.mv_data)[i]);
        }
        fprintf(fp, "|");

        // 写入值
        fprintf(fp, "%zu|", data.mv_size);
        for (size_t i = 0; i < data.mv_size; i++) {
            fprintf(fp, "%02x", ((unsigned char *)data.mv_data)[i]);
        }
        fprintf(fp, "\n");

        count++;
        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    fclose(fp);

    printf("Exported %d records to %s\n", count, filename);
    return 0;
}

// 从文本文件导入数据库
int import_db(MDB_env *env, const char *filename) {
    MDB_txn *txn;
    MDB_dbi dbi;
    FILE *fp;
    char line[4096];
    int rc;
    int count = 0;
    int batch_count = 0;
    const int BATCH_SIZE = 1000;

    printf("Importing database from %s...\n", filename);

    fp = fopen(filename, "r");
    if (fp == NULL) {
        perror("fopen");
        return -1;
    }

    // 开始事务
    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc != 0) {
        fprintf(stderr, "mdb_txn_begin failed: %s\n", mdb_strerror(rc));
        fclose(fp);
        return -1;
    }

    rc = mdb_dbi_open(txn, NULL, 0, &dbi);
    if (rc != 0) {
        fprintf(stderr, "mdb_dbi_open failed: %s\n", mdb_strerror(rc));
        mdb_txn_abort(txn);
        fclose(fp);
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        // 跳过注释和空行
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }

        // 解析行: key_size|key_hex|value_size|value_hex
        char *key_size_str = strtok(line, "|");
        char *key_hex = strtok(NULL, "|");
        char *value_size_str = strtok(NULL, "|");
        char *value_hex = strtok(NULL, "|\n");

        if (!key_size_str || !key_hex || !value_size_str || !value_hex) {
            continue;
        }

        MDB_val key, data;
        key.mv_size = atoi(key_size_str);
        data.mv_size = atoi(value_size_str);

        // 分配缓冲区
        unsigned char *key_buf = malloc(key.mv_size);
        unsigned char *data_buf = malloc(data.mv_size);

        // 从十六进制转换
        for (size_t i = 0; i < key.mv_size; i++) {
            sscanf(key_hex + i * 2, "%2hhx", &key_buf[i]);
        }
        for (size_t i = 0; i < data.mv_size; i++) {
            sscanf(value_hex + i * 2, "%2hhx", &data_buf[i]);
        }

        key.mv_data = key_buf;
        data.mv_data = data_buf;

        rc = mdb_put(txn, dbi, &key, &data, 0);
        if (rc != 0) {
            fprintf(stderr, "mdb_put failed: %s\n", mdb_strerror(rc));
        } else {
            count++;
            batch_count++;
        }

        free(key_buf);
        free(data_buf);

        // 定期提交以避免事务过大
        if (batch_count >= BATCH_SIZE) {
            mdb_txn_commit(txn);
            printf("Imported %d records (batch)...\n", count);

            rc = mdb_txn_begin(env, NULL, 0, &txn);
            if (rc != 0) {
                fprintf(stderr, "mdb_txn_begin failed: %s\n", mdb_strerror(rc));
                fclose(fp);
                return -1;
            }

            mdb_dbi_open(txn, NULL, 0, &dbi);
            batch_count = 0;
        }
    }

    // 提交剩余的记录
    mdb_txn_commit(txn);
    fclose(fp);

    printf("Imported %d records from %s\n", count, filename);
    return 0;
}

// 显示数据库统计信息
void show_db_stats(MDB_env *env) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_stat stat;
    MDB_envinfo info;
    int rc;

    printf("\n========== Database Statistics ==========\n");

    // 环境信息
    mdb_env_info(env, &info);
    printf("Environment:\n");
    printf("  Map size:      %.2f MB\n", (double)info.me_mapsize / (1024 * 1024));
    printf("  Last page:     %zu\n", (size_t)info.me_last_pgno);
    printf("  Max readers:   %u\n", info.me_maxreaders);
    printf("  Num readers:   %u\n", info.me_numreaders);

    // 数据库统计
    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    if (rc == 0) {
        rc = mdb_dbi_open(txn, NULL, 0, &dbi);
        if (rc == 0) {
            mdb_stat(txn, dbi, &stat);
            printf("\nMain Database:\n");
            printf("  Page size:      %u bytes\n", stat.ms_psize);
            printf("  Tree depth:     %u\n", stat.ms_depth);
            printf("  Branch pages:   %zu\n", (size_t)stat.ms_branch_pages);
            printf("  Leaf pages:     %zu\n", (size_t)stat.ms_leaf_pages);
            printf("  Overflow pages: %zu\n", (size_t)stat.ms_overflow_pages);
            printf("  Total entries:  %zu\n", (size_t)stat.ms_entries);

            mdb_dbi_close(env, dbi);
        }
        mdb_txn_abort(txn);
    }

    printf("==========================================\n\n");
}

// 备份数据库
int backup_db(const char *src_path, const char *dest_path) {
    char cmd[512];
    int rc;

    printf("Backing up database from %s to %s...\n", src_path, dest_path);

    // 创建目标目录
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", dest_path);
    system(cmd);

    // 使用 mdb_copy 备份
    snprintf(cmd, sizeof(cmd), "mdb_copy %s %s", src_path, dest_path);
    rc = system(cmd);

    if (rc == 0) {
        printf("Backup completed successfully\n");
        return 0;
    } else {
        fprintf(stderr, "Backup failed with code %d\n", rc);
        return -1;
    }
}

// 打印使用说明
void print_usage(const char *progname) {
    printf("Usage: %s <command> [options]\n\n", progname);
    printf("Commands:\n");
    printf("  stats <db_path>           Show database statistics\n");
    printf("  export <db_path> <file>   Export database to file\n");
    printf("  import <db_path> <file>   Import database from file\n");
    printf("  backup <src> <dest>       Backup database\n");
    printf("  help                      Show this help\n");
    printf("\nExamples:\n");
    printf("  %s stats ./testdb\n", progname);
    printf("  %s export ./testdb export.txt\n", progname);
    printf("  %s import ./testdb export.txt\n", progname);
    printf("  %s backup ./testdb ./backup\n", progname);
}

int main(int argc, char **argv) {
    MDB_env *env;
    char *db_path;
    int rc;

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    char *command = argv[1];

    // stats 命令
    if (strcmp(command, "stats") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s stats <db_path>\n", argv[0]);
            return 1;
        }
        db_path = argv[2];

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

        show_db_stats(env);
        mdb_env_close(env);

    } else if (strcmp(command, "export") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s export <db_path> <file>\n", argv[0]);
            return 1;
        }
        db_path = argv[2];
        char *filename = argv[3];

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

        rc = export_db(env, filename);
        mdb_env_close(env);
        return rc;

    } else if (strcmp(command, "import") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s import <db_path> <file>\n", argv[0]);
            return 1;
        }
        db_path = argv[2];
        char *filename = argv[3];

        rc = mdb_env_create(&env);
        if (rc != 0) {
            fprintf(stderr, "mdb_env_create failed: %s\n", mdb_strerror(rc));
            return 1;
        }

        mdb_env_set_mapsize(env, 1024 * 1024 * 100);

        rc = mdb_env_open(env, db_path, 0, 0664);
        if (rc != 0) {
            fprintf(stderr, "mdb_env_open failed: %s\n", mdb_strerror(rc));
            mdb_env_close(env);
            return 1;
        }

        rc = import_db(env, filename);
        mdb_env_close(env);
        return rc;

    } else if (strcmp(command, "backup") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s backup <src> <dest>\n", argv[0]);
            return 1;
        }

        return backup_db(argv[2], argv[3]);

    } else if (strcmp(command, "help") == 0) {
        print_usage(argv[0]);
    } else {
        fprintf(stderr, "Unknown command: %s\n", command);
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}
