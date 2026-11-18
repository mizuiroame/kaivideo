// Which pieces of a file read over the network to keep and to fetch next: a
// window of blocks from where it's being read. Pure logic; mediafile.c does
// the fetching.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define BLOCK_SIZE (1024 * 1024)
#define BLOCK_SLOTS 12
#define BLOCK_AHEAD 8  // Blocks kept ready from the one being read.

typedef enum { BLOCK_EMPTY, BLOCK_LOADING, BLOCK_READY } BlockState;

typedef struct {
    int64_t index;
    BlockState state;
    size_t size;  // Bytes it holds, when ready.
} BlockSlot;

// The slot holding this block (loading or ready), or -1.
int blockFind(const BlockSlot *slots, int count, int64_t index);
// The next block to fetch for a reader at read_block, or -1 if the window is
// complete. total_blocks < 0: the file's length isn't known yet.
int64_t blockNextWanted(const BlockSlot *slots, int count, int64_t read_block, int64_t total_blocks);
// The slot to put a new block in: an empty one, else the block furthest behind
// the reader, else the one furthest past the window. -1 if none can go.
int blockVictim(const BlockSlot *slots, int count, int64_t read_block);
