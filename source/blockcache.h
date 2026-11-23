// Which pieces of a file read over the network to keep and to fetch next: a
// window of blocks from where each of its readers is. Pure logic; mediafile.c
// does the fetching.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BLOCK_SIZE (1024 * 1024)
#define BLOCK_SLOTS 24
#define BLOCK_AHEAD 8  // Blocks kept ready from the one each reader is at.

typedef enum { BLOCK_EMPTY, BLOCK_LOADING, BLOCK_READY } BlockState;

typedef struct {
    int64_t index;
    BlockState state;
    size_t size;  // Bytes it holds, when ready.
} BlockSlot;

// The slot holding this block (loading or ready), or -1.
int blockFind(const BlockSlot *slots, int count, int64_t index);
// True if a reader at one of these blocks wants this one kept.
bool blockIsWanted(int64_t index, const int64_t *readers, int reader_count);
// The next block to fetch, the nearest to any reader, or -1 if all the windows
// are complete. total_blocks < 0: the file's length isn't known yet.
int64_t blockNextWanted(const BlockSlot *slots, int count, const int64_t *readers, int reader_count,
                        int64_t total_blocks);
// The same, but keeping to the order of the file where it can: a block a reader is at (needed now) still
// comes first, nearest to after's; else the block after `after` (the last fetched) if a reader will want
// it; else blockNextWanted's. For a server that reads a file as one stream (FTP), going back and forth
// between two readers would start a new transfer each time.
int64_t blockNextInOrder(const BlockSlot *slots, int count, const int64_t *readers, int reader_count,
                         int64_t total_blocks, int64_t after);
// The slot to put a new block in: an empty one, else the block furthest behind
// the readers, else the one furthest past them. -1 if none can go.
int blockVictim(const BlockSlot *slots, int count, const int64_t *readers, int reader_count);
