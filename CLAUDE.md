# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

LMDB (Lightning Memory-Mapped Database) is an ultra-fast, memory-efficient key-value embedded database. It is a Btree-based database management library modeled loosely on the BerkeleyDB API but much simplified. The entire database is exposed in a memory map, and all data fetches return data directly from the mapped memory - no malloc's or memcpy's occur during data fetches.

**Key characteristics:**
- Fully transactional with full ACID semantics
- Zero-copy architecture - data returned directly from memory map
- Multi-version concurrency control (MVCC) - readers never block writers, writers never block readers
- Copy-on-write strategy - no active data pages are ever overwritten
- No maintenance required during operation (free pages are tracked and reused)
- Single-writer design (only one write transaction at a time) - writers can never deadlock

## Repository Structure

The codebase is exceptionally flat - all source files are in `libraries/liblmdb/`:

```
libraries/liblmdb/
├── mdb.c          # Core database engine (11,478 lines) - all main logic
├── lmdb.h         # Public API header with comprehensive documentation
├── midl.c/h       # ID list implementation (internal data structures)
├── mdb_*.c        # Utility tools (stat, copy, dump, load, drop)
├── mtest*.c       # Test programs
├── mplay.c        # Log replay and stress testing tool
└── Makefile       # Build system
```

## Building

From `libraries/liblmdb/`:

```bash
make              # Build static library (liblmdb.a), shared library (liblmdb.so), and tools
make clean        # Remove all build artifacts
make install      # Install libraries, headers, and tools (prefix: /usr/local)
```

**Build outputs:**
- `liblmdb.a` - Static library
- `liblmdb.so` - Shared library
- Tools: `mdb_stat`, `mdb_copy`, `mdb_dump`, `mdb_load`, `mdb_drop`
- Test programs: `mtest`, `mtest2`-`mtest6`, `mplay`

**Compiler flags:**
- Requires `-pthread` (threaded compilation mandatory)
- Warning flags: `-W -Wall -Wno-unused-parameter -Wbad-function-cast -Wuninitialized`
- Optimization: `-O2 -g`

**Configuration preprocessor macros** (see Makefile and mdb.c for details):
- `MDB_USE_POSIX_MUTEX`, `MDB_USE_POSIX_SEM`, `MDB_USE_SYSV_SEM` - Threading primitives
- `MDB_DSYNC`, `MDB_FDATASYNC` - Sync options
- `MDB_USE_PWRITEV` - Write optimization
- `MDB_USE_ROBUST` - Robust mutex support

## Testing

```bash
make test         # Run basic tests (builds if needed)
```

The test target:
1. Removes and recreates the `testdb` directory
2. Runs `mtest` and `mdb_stat testdb`

**Individual test programs:**
- `./mtest` - Basic functionality test with random data
- `./mtest2` through `./mtest6` - Additional functionality tests
- `./mplay` - Performance/stress testing and log replay tool

**Coverage testing:**
```bash
make coverage     # Build with coverage flags and run all tests
```

## Code Architecture

### Core Components

**mdb.c** - The entire database engine in one file:
- Memory management (mmap handling)
- Transaction handling (begin, commit, abort)
- Btree operations (insert, delete, lookup)
- Lock management (reader/writer concurrency)
- Page management (allocation, free list tracking)
- Cursor operations

**Key design patterns:**
1. **Memory-mapped I/O** - Uses `mmap()` (or platform equivalents) to map the entire database file into memory
2. **MVCC** - Multiple versions of pages coexist; readers see consistent snapshots
3. **Copy-on-write** - Modified pages are written to new locations; old pages remain for active readers
4. **Single writer** - Only one write transaction can be active at a time (prevents write deadlocks)
5. **Reader slots** - Fixed-size array in lock file tracks active reader transactions

### Transaction Model

- **Read transactions**: Use `mdb_txn_begin(env, NULL, MDB_RDONLY, &txn)`
- **Write transactions**: Use `mdb_txn_begin(env, NULL, 0, &txn)`
- **Nested transactions**: Use `mdb_txn_begin(env, parent, 0, &txn)`
- Thread affinity: A transaction must be used in the thread that created it (unless `MDB_NOTLS` flag is set)

### Database Handles (DBI)

Database handles are created within a transaction and can be used by subsequent transactions:
```c
mdb_dbi_open(txn, NULL, 0, &dbi);  // Open main database
// ... use dbi in other transactions ...
mdb_dbi_close(env, dbi);           // Close when done
```

### API Naming Conventions

- `mdb_env_*` - Environment-level operations (open, close, sync, info)
- `mdb_txn_*` - Transaction operations (begin, commit, abort, id)
- `mdb_dbi_*` - Database handle operations (open, close, flags)
- `mdb_cursor_*` - Cursor operations (open, close, get, put, del)
- `mdb_get`, `mdb_put` - Direct key/value operations
- `mdb_drop` - Delete database or all records

## Important Implementation Details

### Platform-Specific Code

mdb.c contains extensive platform-specific code:
- **Windows**: Uses native NT APIs (`NtCreateSection`, `NtMapViewOfSection`) for incremental file growth
- **MIPS/Linux**: Requires explicit cache flushing (`cacheflush()`)
- **Linux**: Has workaround for broken `fdatasync()` on ext3/ext4 on old kernels
- **BSD/macOS**: Different semaphore and mutex behavior

### Lock File Management

The lock file (`lock.mdb` or similar) is critical:
- Tracks reader transactions (fixed-size array of reader slots)
- Coordinates writer access
- Can become stale if processes crash abnormally
- Use `mdb_reader_check()` or `mdb_stat` tool to detect stale readers

### Memory Map Behavior

- **Read-only mode** (default): Immune to corruption from stray pointer writes
- **Read-write mode**: Higher write performance but vulnerable to stray writes
- Database size is reserved upfront (address space, not actual disk usage)

## Common Development Workflow

1. Modify code (primarily in `mdb.c`)
2. Test: `make test`
3. Test individual scenarios: `./mtest2`, `./mtest3`, etc.
4. Check for memory leaks: `valgrind ./mtest` (if available)
5. Install: `make install`

## Documentation

- **lmdb.h** - Comprehensive API documentation with Doxygen comments
- **Doxyfile** - Doxygen configuration for generating HTML docs
- **intro.doc** - Getting started guide (HTML format)
- **sample-mdb.txt** - Simple usage example code
- **sample-bdb.txt** - BerkeleyDB API comparison example
- **mdb_*.1** - Man pages for utility tools

Generate documentation:
```bash
doxygen Doxyfile    # Generates HTML documentation
```

## Git History

Commit messages reference ITS (Issue Tracking System) numbers:
- Example: `ITS#10420 LMDB: add support for Haiku`
- Use these to understand the context of changes

## License

OpenLDAP Public License - see LICENSE file.
