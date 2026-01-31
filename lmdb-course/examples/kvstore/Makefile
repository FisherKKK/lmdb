# Makefile for Key-Value Store Project

CC = gcc
CFLAGS = -Wall -Wextra -g
LMBDDIR = ../../../libraries/liblmdb
LDFLAGS = $(LMBDDIR)/liblmdb.a -lpthread -lrt
CFLAGS += -I$(LMBDDIR)

OBJS = kvstore.o
TARGETS = kv_client

all: $(TARGETS)

kv_client: kv_client.o $(OBJS)
	@echo "Building $@..."
	@$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built $@ successfully"

%.o: %.c
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

clean:
	@echo "Cleaning..."
	@rm -f $(TARGETS) $(OBJS) kv_client.o
	@rm -rf kvstore_db

.PHONY: all clean
