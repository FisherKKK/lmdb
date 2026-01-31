#!/bin/bash
# learn.sh - LMDB Course Interactive Learning Script
#
# This script guides you through the LMDB course step by step

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Print colored message
print_header() {
    echo -e "${CYAN}========================================${NC}"
    echo -e "${CYAN}$1${NC}"
    echo -e "${CYAN}========================================${NC}"
}

print_step() {
    echo -e "\n${GREEN}==>${NC} $1"
}

print_info() {
    echo -e "${BLUE}ℹ${NC} $1"
}

print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}⚠${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
}

# Pause for user
pause() {
    echo -e "\n${YELLOW}Press Enter to continue...${NC}"
    read
}

# Check prerequisites
check_prerequisites() {
    print_header "Checking Prerequisites"

    # Check if gcc is installed
    if command -v gcc &> /dev/null; then
        print_success "gcc is installed"
    else
        print_error "gcc is not installed. Please install build-essential:"
        echo "  sudo apt-get install build-essential"
        exit 1
    fi

    # Check if make is installed
    if command -v make &> /dev/null; then
        print_success "make is installed"
    else
        print_error "make is not installed"
        exit 1
    fi

    # Check if LMDB library is available
    if [ -f "../../libraries/liblmdb/liblmdb.a" ]; then
        print_success "LMDB library found"
    else
        print_warning "LMDB library not found. Building..."
        (cd ../../libraries/liblmdb && make)
        print_success "LMDB library built"
    fi

    # Check if examples are built
    if [ -f "./first_example" ]; then
        print_success "Example programs are already built"
    else
        print_info "Building example programs..."
        make
        print_success "Example programs built"
    fi

    pause
}

# Day 1: Introduction
day1_intro() {
    print_header "Day 1: LMDB Overview & First Program"

    print_step "What is LMDB?"
    echo "LMDB (Lightning Memory-Mapped Database) is an ultra-fast key-value store."
    echo ""
    echo "Key features:"
    echo "  • Memory-mapped I/O for zero-copy reads"
    echo "  • B+tree storage with O(log n) operations"
    echo "  • MVCC for concurrent reads"
    echo "  • Single writer, multiple readers model"
    echo "  • ACID transactions"
    echo "  • No background processes needed"

    pause

    print_step "Running first_example"
    print_info "This program demonstrates the basic LMDB workflow:"
    echo "  1. Create environment"
    echo "  2. Set map size"
    echo "  3. Open database"
    echo "  4. Begin transaction"
    echo "  5. Put/Get data"
    echo "  6. Commit transaction"
    echo ""

    ./first_example

    pause
    print_success "You completed Day 1!"
}

# Day 2: Memory-Mapped I/O
day2_mmap() {
    print_header "Day 2: Memory-Mapped I/O"

    print_step "Understanding mmap"
    print_info "LMDB uses memory-mapped files for zero-copy data access."
    echo ""
    echo "Traditional I/O vs mmap:"
    echo "  Traditional: disk → kernel buffer → user buffer"
    echo "  mmap:        disk → kernel buffer (mapped to user space)"
    echo ""
    echo "Benefits:"
    echo "  • No data copying between kernel and user space"
    echo "  • OS handles paging automatically"
    echo "  • Simplified code"

    pause

    print_step "Running mmap_benchmark"
    print_info "Comparing default mode vs WRITEMAP mode..."
    echo ""

    ./mmap_benchmark

    pause
    print_success "You completed Day 2!"
}

# Day 3: Database Environment
day3_env() {
    print_header "Day 3: Database Environment"

    print_step "Environment Management"
    print_info "The MDB_env structure manages the entire database environment."
    echo ""
    echo "Key settings:"
    echo "  • Map size: Must be set before opening"
    echo "  • Max databases: Limit named databases"
    echo "  • Max readers: Concurrent read transactions"

    pause

    print_step "Running env_demo"
    print_info "Demonstrating environment configuration and multiple databases..."
    echo ""

    ./env_demo

    pause

    print_step "Using db_tools"
    print_info "Checking database statistics..."
    echo ""

    ./db_tools stats ./testdb

    pause
    print_success "You completed Day 3!"
}

# Day 6: Transactions
day6_transactions() {
    print_header "Day 6: Transaction Management"

    print_step "Nested Transactions"
    print_info "LMDB supports nested transactions with these properties:"
    echo "  • Child sees parent's data"
    echo "  • Parent doesn't see child's uncommitted data"
    echo "  • Committed child merges to parent"
    echo "  • Aborted child discards changes"

    pause

    print_step "Running nested_txn_demo"
    echo ""

    ./nested_txn_demo

    pause
    print_success "You completed Day 6!"
}

# Day 9: Cursors
day9_cursors() {
    print_header "Day 9: Cursor Operations"

    print_step "Understanding Cursors"
    print_info "Cursors provide efficient data traversal:"
    echo "  • Maintain position in database"
    echo "  • Support range queries"
    echo "  • Enable sequential access"
    echo ""
    echo "Cursor operations:"
    echo "  • MDB_FIRST/MDB_LAST: Jump to beginning/end"
    echo "  • MDB_NEXT/MDB_PREV: Move sequentially"
    echo "  • MDB_SET: Jump to specific key"
    echo "  • MDB_SET_RANGE: Find key or next greater"

    pause

    print_step "Running cursor_demo"
    echo ""

    ./cursor_demo

    pause
    print_success "You completed Day 9!"
}

# Day 14: Concurrent Access
day14_concurrent() {
    print_header "Day 14: Concurrent Access & Performance"

    print_step "Multi-threaded Access"
    print_info "LMDB's concurrency model:"
    echo "  • Unlimited concurrent readers"
    echo "  • Only one writer at a time"
    echo "  • MVCC prevents blocking"
    echo "  • Writers retry on MDB_TXN_FULL"

    pause

    print_step "Running concurrent_demo"
    print_info "This demonstrates 5 readers and 2 writers operating concurrently..."
    echo ""

    ./concurrent_demo

    pause

    print_step "Performance Testing"
    print_info "Running comprehensive performance tests..."
    echo ""

    ./perf_test ./testdb 5000

    pause
    print_success "You completed Day 14!"
}

# Stress test
stress_test() {
    print_header "Optional: Stress Testing"

    print_step "About Stress Testing"
    print_info "Stress testing verifies:"
    echo "  • Concurrent access stability"
    echo "  • Data integrity under load"
    echo "  • Memory leak detection"
    echo "  • Lock handling"
    echo ""
    print_warning "This runs for 60 seconds. Press Ctrl+C to stop early."

    pause

    print_step "Running stress_test"
    echo ""

    timeout 65 ./stress_test ./testdb || true

    pause
    print_success "Stress test completed!"
}

# Summary
summary() {
    print_header "Course Summary"

    echo "Congratulations! You've completed the LMDB Course practical examples."
    echo ""
    echo "What you've learned:"
    echo "  ✓ Basic LMDB operations"
    echo "  ✓ Memory-mapped I/O concepts"
    echo "  ✓ Environment configuration"
    echo "  ✓ Transaction management"
    echo "  ✓ Cursor operations"
    echo "  ✓ Concurrent access patterns"
    echo "  ✓ Performance testing"
    echo ""
    echo "Next steps:"
    echo "  1. Read the detailed course materials in ../"
    echo "  2. Study the source code in ../../libraries/liblmdb/"
    echo "  3. Experiment with the example programs"
    echo "  4. Build your own LMDB application"
    echo ""
    echo "Available tools:"
    echo "  • db_tools - Database utilities (stats, export, import, backup)"
    echo "  • perf_test - Performance testing"
    echo "  • stress_test - Stress testing"
    echo ""
    echo "For help:"
    echo "  ./db_tools help"
    echo "  make help"
    echo ""
    echo -e "${GREEN}Happy Learning!${NC}"
}

# Main menu
main_menu() {
    while true; do
        print_header "LMDB Course - Interactive Learning"
        echo ""
        echo "1. Prerequisites check"
        echo "2. Day 1: First LMDB Program"
        echo "3. Day 2: Memory-Mapped I/O"
        echo "4. Day 3: Database Environment"
        echo "5. Day 6: Transaction Management"
        echo "6. Day 9: Cursor Operations"
        echo "7. Day 14: Concurrent Access"
        echo "8. Stress Test (Optional)"
        echo "9. Run All Examples"
        echo "0. Exit"
        echo ""
        echo -n "${YELLOW}Choose an option [0-9]: ${NC}"

        read choice

        case $choice in
            1) check_prerequisites ;;
            2) day1_intro ;;
            3) day2_mmap ;;
            4) day3_env ;;
            5) day6_transactions ;;
            6) day9_cursors ;;
            7) day14_concurrent ;;
            8) stress_test ;;
            9)
                check_prerequisites
                day1_intro
                day2_mmap
                day3_env
                day6_transactions
                day9_cursors
                day14_concurrent
                summary
                break
                ;;
            0)
                echo "Goodbye!"
                exit 0
                ;;
            *)
                print_error "Invalid option. Please try again."
                ;;
        esac
    done
}

# Main script
if [ "$1" == "--all" ]; then
    check_prerequisites
    day1_intro
    day2_mmap
    day3_env
    day6_transactions
    day9_cursors
    day14_concurrent
    summary
else
    main_menu
fi
