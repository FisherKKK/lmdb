# Makefile for Task Queue Project

CC = gcc
CFLAGS = -Wall -Wextra -g
LMBDDIR = ../../../libraries/liblmdb
LDFLAGS = $(LMBDDIR)/liblmdb.a -lpthread -lrt
CFLAGS += -I$(LMBDDIR)

OBJS = taskqueue.o
TARGETS = tq_client tq_worker

all: $(TARGETS)

tq_client: tq_client.o $(OBJS)
	@echo "Building $@..."
	@$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built $@ successfully"

tq_worker: tq_worker.o $(OBJS)
	@echo "Building $@..."
	@$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built $@ successfully"

%.o: %.c
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

clean:
	@echo "Cleaning..."
	@rm -f $(TARGETS) $(OBJS) tq_client.o tq_worker.o
	@rm -rf taskqueue_db

.PHONY: all clean
