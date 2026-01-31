# Makefile for LMDB Course Examples
# Course: LMDB 底层实现 14天课程

# Compiler settings
CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS = -llmdb -lrt
LMBDDIR = ../../libraries/liblmdb

# LMDB library path
ifeq ($(wildcard $(LMBDDIR)/liblmdb.a),)
    # Try system library
    LDFLAGS = -llmdb -lrt
else
    # Use static linking with local library
    LDFLAGS = $(LMBDDIR)/liblmdb.a -lpthread -lrt
    CFLAGS += -I$(LMBDDIR)
endif

# Target executables
TARGETS = first_example \
          mmap_benchmark \
          mmap_explorer \
          env_demo \
          page_viewer \
          tree_visualizer \
          custom_compare \
          cursor_demo \
          nested_txn_demo \
          txn_tracker \
          commit_analyzer \
          mvcc_demo \
          internals_demo \
          btree_impl \
          page_allocator \
          code_explorer \
          lmdb_debugger \
          concurrent_demo \
          db_tools \
          db_compare \
          bulk_loader \
          perf_test \
          stress_test \
          visualize_db \
          cache_simulator

# Default target
all: $(TARGETS)

# Pattern rule for building executables
%: %.c
	@echo "Building $@..."
	@$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo "Built $@ successfully"

# Individual targets
first_example: first_example.c
mmap_benchmark: mmap_benchmark.c
env_demo: env_demo.c
cursor_demo: cursor_demo.c
nested_txn_demo: nested_txn_demo.c
lmdb_debugger: lmdb_debugger.c
concurrent_demo: concurrent_demo.c
db_tools: db_tools.c
perf_test: perf_test.c
stress_test: stress_test.c
visualize_db: visualize_db.c
cache_simulator: cache_simulator.c

# Clean target
clean:
	@echo "Cleaning built files..."
	@rm -f $(TARGETS)
	@rm -rf ./testdb* ./testdb_default ./testdb_writemap
	@echo "Clean complete"

# Run tests
test: all
	@echo "Running examples..."
	@echo ""
	@echo "=== Running first_example ==="
	./first_example
	@echo ""
	@echo "=== Running cursor_demo ==="
	./cursor_demo
	@echo ""
	@echo "=== Running nested_txn_demo ==="
	./nested_txn_demo
	@echo ""
	@echo "Tests complete"

# Install LMDB library if needed
install-lmdb:
	@echo "Building LMDB library..."
	@(cd $(LMBDDIR) && make clean && make)
	@echo "LMDB library built successfully"

# Help target
help:
	@echo "LMDB Course Examples - Makefile"
	@echo ""
	@echo "Available targets:"
	@echo "  all          - Build all example programs (default)"
	@echo "  test         - Build and run examples"
	@echo "  clean        - Remove built files and test databases"
	@echo "  install-lmdb - Build LMDB library from source"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Example programs by day:"
	@echo "  Day 1:"
	@echo "    first_example      - Basic LMDB usage"
	@echo ""
	@echo "  Day 2:"
	@echo "    mmap_benchmark     - mmap mode performance comparison"
	@echo "    mmap_explorer      - Memory-mapped I/O exploration"
	@echo ""
	@echo "  Day 3:"
	@echo "    env_demo           - Environment management demo"
	@echo ""
	@echo "  Day 4:"
	@echo "    page_viewer        - Page structure visualization"
	@echo ""
	@echo "  Day 5:"
	@echo "    tree_visualizer    - B+tree structure visualization"
	@echo "    custom_compare     - Custom comparison function demo"
	@echo ""
	@echo "  Day 6:"
	@echo "    nested_txn_demo    - Nested transaction demo"
	@echo "    txn_tracker        - Transaction lifecycle tracker"
	@echo ""
	@echo "  Day 7:"
	@echo "    commit_analyzer    - Transaction commit analysis"
	@echo ""
	@echo "  Day 8:"
	@echo "    mvcc_demo          - MVCC demonstration"
	@echo ""
	@echo "  Day 9:"
	@echo "    cursor_demo        - Cursor usage demonstration"
	@echo ""
	@echo "  Day 14:"
	@echo "    lmdb_debugger      - LMDB debugging tool"
	@echo ""
	@echo "  Internal implementation (source code deep dive):"
	@echo "    internals_demo     - Internal data structures"
	@echo "    btree_impl         - B+tree implementation details"
	@echo "    page_allocator     - Page allocation and freelist"
	@echo "    code_explorer      - Source code navigation guide"
	@echo ""
	@echo "  Advanced tools:"
	@echo "    concurrent_demo    - Multi-threaded concurrent access"
	@echo "    db_tools           - Export/import/stats/backup utilities"
	@echo "    db_compare         - Database comparison tool"
	@echo "    bulk_loader        - High-performance bulk import/export"
	@echo "    perf_test          - Performance test suite"
	@echo "    stress_test        - Stress testing"
	@echo "    visualize_db       - Database visualization"
	@echo "    cache_simulator    - Page cache simulator"
	@echo ""
	@echo "Usage:"
	@echo "  make              # Build all examples"
	@echo "  make test         # Build and run examples"
	@echo "  make clean        # Clean build artifacts"
	@echo ""
	@echo "Learning path:"
	@echo "  1. Run internals_demo    - Understand data structures"
	@echo "  2. Run btree_impl         - Learn B+tree algorithms"
	@echo "  3. Run page_allocator    - Understand page management"
	@echo "  4. Run code_explorer      - Navigate mdb.c source"
	@echo "  5. Read INTERNALS-GUIDE.md - Deep dive reference"

# Phony targets
.PHONY: all clean test install-lmdb help
