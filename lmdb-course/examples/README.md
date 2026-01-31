# LMDB Course Examples

This directory contains example programs from the LMDB 底层实现 14天课程.

## Quick Start

### Interactive Learning

The easiest way to learn is to use the interactive learning script:

```bash
./learn.sh
```

This will guide you through all the examples step by step with explanations.

To run all examples automatically:
```bash
./learn.sh --all
```

## Building the Examples

### Prerequisites

1. LMDB library must be installed or built:
```bash
cd ../../libraries/liblmdb
make
```

2. Or install system-wide:
```bash
# Ubuntu/Debian
sudo apt-get install liblmdb-dev

# macOS
brew install lmdb

# Fedora/RHEL
sudo dnf install lmdb-devel
```

### Compile All Examples

```bash
make
```

### Compile Individual Examples

```bash
make first_example
make mmap_benchmark
make env_demo
make cursor_demo
make nested_txn_demo
make lmdb_debugger
make concurrent_demo
make db_tools
make perf_test
make stress_test
```

## Example Programs

### Core Learning Examples

#### Day 1: LMDB Overview

**first_example.c** - First LMDB program demonstrating basic operations.

```bash
./first_example
```

**What you'll learn:**
- Creating an environment
- Opening a database
- Writing data
- Reading data
- Proper cleanup

#### Day 2: Memory-Mapped I/O

**mmap_benchmark.c** - Compares performance between default mode and WRITEMAP mode.

```bash
./mmap_benchmark
```

**What you'll learn:**
- Memory mapping performance
- Impact of WRITEMAP flag
- Benchmarking techniques

**mmap_explorer.c** - Explores memory mapping behavior and page faults.

```bash
./mmap_explorer
```

**What you'll learn:**
- How memory mapping works
- Page fault behavior
- Sequential vs random access patterns
- LMDB mode comparisons

#### Day 3: Database Environment

**env_demo.c** - Demonstrates environment management and configuration.

```bash
./env_demo
```

**What you'll learn:**
- Environment configuration options
- Multiple databases
- Reader table monitoring
- Environment statistics

#### Day 4: Page Structure

**page_viewer.c** - Visualizes LMDB page structure and layout.

```bash
./page_viewer
```

**What you'll learn:**
- Page header structure
- Branch vs leaf pages
- Overflow pages
- Free space management
- Page organization

#### Day 5: B+Tree Implementation

**tree_visualizer.c** - Visualizes B+tree structure and operations.

```bash
./tree_visualizer
```

**What you'll learn:**
- B+tree organization
- Search, insert, delete operations
- Tree balancing
- Splits and merges

**custom_compare.c** - Demonstrates custom comparison functions.

```bash
./custom_compare
```

**What you'll learn:**
- Custom key ordering
- Numeric string comparison
- Case-insensitive sorting
- Multi-field keys
- Reverse ordering

#### Day 6: Transaction Management

**nested_txn_demo.c** - Demonstrates nested transaction behavior.

```bash
./nested_txn_demo
```

**What you'll learn:**
- Parent-child transaction relationships
- Data visibility between transactions
- Commit and abort behavior
- Transaction isolation

**txn_tracker.c** - Tracks transaction lifecycle.

```bash
./txn_tracker
```

**What you'll learn:**
- Transaction states
- Lifecycle visualization
- Nested transaction tracking
- Transaction timing

#### Day 7: Transaction Management (Advanced)

**commit_analyzer.c** - Analyzes transaction commit process.

```bash
./commit_analyzer
```

**What you'll learn:**
- Commit process steps
- Copy-on-write visualization
- Meta page management
- Commit performance factors
- Batch optimization

#### Day 8: MVCC

**mvcc_demo.c** - Demonstrates Multi-Version Concurrency Control.

```bash
./mvcc_demo
```

**What you'll learn:**
- MVCC version management
- Readers don't block writers
- Consistent snapshots
- Reader table management
- Version chains

#### Day 9: Cursor Implementation

**cursor_demo.c** - Shows cursor usage for data traversal.

```bash
./cursor_demo
```

**What you'll learn:**
- Creating and using cursors
- Forward and backward traversal
- Range queries
- Key-based searches

#### Day 14: Advanced Topics

**lmdb_debugger.c** - Comprehensive debugging and diagnostic tool.

```bash
./lmdb_debugger [path_to_database]
```

**What you'll learn:**
- Environment status checking
- Database health assessment
- Performance testing
- Diagnostic techniques

### Internals Deep Dive (底层实现深入)

**internals_demo.c** - LMDB 内部数据结构深度解析。

```bash
./internals_demo
```

**What you'll learn:**
- MDB_env 环境结构
- MDB_txn 事务结构
- MDB_page 页面结构
- MDB_node 节点结构
- MDB_cursor 游标结构
- 读者表管理

**btree_impl.c** - B+ 树实现详细演示。

```bash
./btree_impl
```

**What you'll learn:**
- B+ 树搜索算法
- 页面分裂过程
- 根节点分裂（树增长）
- 写时复制与页面重用
- 溢出页链管理

**page_allocator.c** - 页面分配器演示。

```bash
./page_allocator
```

**What you'll learn:**
- 页面生命周期
- 空闲列表结构
- 页面分配算法
- 事务与页面的关系
- 页面重用时机
- 性能考虑

**code_explorer.c** - 源代码导航助手。

```bash
./code_explorer
```

**What you'll learn:**
- mdb.c 文件组织
- 关键数据结构位置
- 关键函数位置
- 函数调用流程
- 学习路径建议

### Advanced Examples

**concurrent_demo.c** - Multi-threaded concurrent access demonstration.

```bash
./concurrent_demo
```

**What you'll learn:**
- Multiple concurrent readers
- Single writer with retry logic
- MVCC in action
- Thread safety patterns

**db_tools.c** - Database utility tools (export, import, stats, backup).

```bash
./db_tools help                    # Show help
./db_tools stats ./testdb          # Database statistics
./db_tools export ./testdb out.txt # Export database
./db_tools import ./testdb in.txt  # Import database
./db_tools backup ./src ./dest     # Backup database
```

**Features:**
- Export database to text format
- Import from text format
- Display database statistics
- Backup databases

**db_compare.c** - Database comparison and verification tool.

```bash
./db_compare ./db_a ./db_b         # Compare two databases
./db_compare -v ./db_a ./db_b      # Verbose mode
./db_compare --sample               # Create and compare sample DBs
```

**Features:**
- Find keys only in one database
- Find keys with different values
- Verify database integrity
- Compare backups

**bulk_loader.c** - High-performance bulk import/export.

```bash
./bulk_loader import ./db ./data.txt 5000     # Import with batch size
./bulk_loader export ./db ./output.txt       # Export to text
./bulk_loader generate ./sample.txt 100000   # Generate sample data
./bulk_loader benchmark 50000                # Benchmark batch sizes
```

**Features:**
- Fast bulk import with batching
- Text-based export/import
- Sample data generation
- Batch size optimization

**perf_test.c** - Comprehensive performance testing suite.

```bash
./perf_test [db_path] [num_operations]
```

**Tests:**
- Sequential write performance
- Batch write performance
- Random read performance
- Range query performance
- Various data sizes

**What you'll learn:**
- Performance characteristics
- Optimal batch sizes
- Read vs write performance
- Impact of data size

**stress_test.c** - Stress testing and stability verification tool.

```bash
./stress_test [db_path]
```

**What it tests:**
- Concurrent access stability
- Transaction integrity
- Memory leaks
- Lock contention handling
- Data consistency

**Options:**
- Runs for 60 seconds by default
- Press Ctrl+C to stop early
- Generates periodic statistics

**visualize_db.c** - Database visualization tool with colored ASCII output.

```bash
./visualize_db [db_path]
```

**Features:**
- B+ tree structure visualization
- Page layout analysis with bar charts
- Key distribution analysis
- Space efficiency metrics
- Health score assessment

**cache_simulator.c** - Page cache behavior simulator.

```bash
./cache_simulator [db_path] [num_pages]
```

**Features:**
- Simulates cache hit/miss rates
- Analyzes page access patterns
- Recommends optimal cache sizes
- Tracks hot/cold pages

## Practice Projects

Complete, production-ready projects demonstrating LMDB in real-world scenarios.

### Task Queue System (taskqueue/)

A multi-process task queue with priority scheduling.

```bash
cd taskqueue
make

# Terminal 1: Start a worker
./tq_worker

# Terminal 2: Add tasks
./tq_client create "Process data" -p 5 -d '{"file":"data.csv"}'
./tq_client create "Send email" -p 8
./tq_client create "Generate report" -p 3

# Check status
./tq_client stats
./tq_client list pending
./tq_client list running
```

**Features:**
- Priority-based task scheduling
- Multi-process worker support
- Task lifecycle tracking (pending → running → completed/failed)
- Task retry mechanism
- Statistics and monitoring

### Key-Value Store (kvstore/)

A feature-rich key-value store with TTL support.

```bash
cd kvstore
make

# Basic operations
./kv_client set user:1 "John Doe" 3600  # Set with 1 hour TTL
./kv_client get user:1                  # Get value
./kv_client delete user:1               # Delete
./kv_client exists user:1               # Check existence

# TTL operations
./kv_client ttl user:1                  # Get remaining TTL
./kv_client ttl user:1 7200            # Set TTL to 2 hours
./kv_client touch user:1                # Refresh TTL

# Counter operations
./kv_client incr counter 5              # Increment by 5

# Namespace operations
./kv_client ns-set cache "session:123" "data" 300
./kv_client ns-get cache "session:123"

# List and cleanup
./kv_client list "user:"                # List keys with prefix
./kv_client cleanup                     # Remove expired keys
./kv_client stats                       # Show statistics
```

**Features:**
- Simple key-value operations (get, set, delete, exists)
- TTL (time-to-live) support with automatic expiration
- Namespaces for key isolation
- Batch operations for better performance
- Atomic increment operations
- Key listing with prefix filtering
- Automatic cleanup of expired keys
- Comprehensive statistics

## Running All Tests

```bash
make test
```

This will build and run several example programs.

## Cleaning Up

Remove built executables and test databases:

```bash
make clean
```

## Course Structure

Each example corresponds to a specific day in the course:

- Day 1: LMDB Overview & Architecture
- Day 2: Memory-Mapped I/O
- Day 3: Database Environment
- Day 4: Page Structure
- Day 5: B+Tree Implementation
- Day 6: Transaction Management (Part 1)
- Day 7: Transaction Management (Part 2)
- Day 8: MVCC & Version Management
- Day 9: Cursor Implementation
- Day 10: Lock Management
- Day 11: Write Operations & Copy-on-Write
- Day 12: Free List & Space Management
- Day 13: Platform-Specific Optimizations
- Day 14: Advanced Topics & Best Practices

## Learning Path

For beginners, follow this order:
1. Run `./learn.sh` for interactive guided learning
2. Run `first_example` to understand the basics
3. Run `env_demo` to learn about environment setup
4. Run `cursor_demo` to understand data traversal
5. Run `nested_txn_demo` to learn about transactions
6. Run `mmap_benchmark` to understand performance
7. Run `concurrent_demo` to understand concurrency
8. Run `perf_test` to understand performance tuning
9. Run `db_tools` to learn database utilities
10. Run `stress_test` to verify stability

## Troubleshooting

### "liblmdb.so: not found" Error

If you get a library not found error:

1. Build LMDB locally:
```bash
make install-lmdb
```

2. Or set LD_LIBRARY_PATH:
```bash
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
```

### Compilation Errors

If you encounter compilation errors:

1. Check that LMDB is installed:
```bash
ls /usr/include/lmdb.h
ls /usr/lib/liblmdb.*
```

2. For manual installation:
```bash
cd ../../libraries/liblmdb
make
sudo make install
```

### Permission Errors

Make sure you have write permissions in the current directory to create test databases:

```bash
chmod +w .
```

## Additional Tools

The LMDB source distribution includes useful tools:

- `mdb_stat` - Display database statistics
- `mdb_copy` - Copy a database
- `mdb_dump` - Dump database to text
- `mdb_load` - Load from text into database
- `mdb_drop` - Delete a database

These are typically built when you compile LMDB:
```bash
cd ../../libraries/liblmdb
make
```

## Makefile Targets

```bash
make              # Build all examples
make clean        # Clean build artifacts and databases
make test         # Build and run tests
make install-lmdb # Build LMDB library
make help         # Show help message
```

## Getting Help

For detailed explanations of each example, refer to the corresponding day's course material in the parent directory.

Course files:
- `../README.md` - Course index and overview
- `../CHEATSHEET.md` - Quick reference card
- `../EXERCISE-SOLUTIONS.md` - Exercise answers
- `../SOURCE-NAVIGATION.md` - Source code guide
- `../Day-*.md` - Daily course materials

## License

These examples are provided for educational purposes as part of the LMDB 底层实现 14天课程.
