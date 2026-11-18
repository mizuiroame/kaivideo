#include "blockcache.h"

#include <stdbool.h>

int blockFind(const BlockSlot *slots, int count, int64_t index)
{
    for (int i = 0; i < count; i++)
        if (slots[i].state != BLOCK_EMPTY && slots[i].index == index)
            return i;
    return -1;
}

int64_t blockNextWanted(const BlockSlot *slots, int count, int64_t read_block, int64_t total_blocks)
{
    for (int64_t index = read_block; index < read_block + BLOCK_AHEAD; index++) {
        if (total_blocks >= 0 && index >= total_blocks)
            return -1;
        if (blockFind(slots, count, index) < 0)
            return index;
        if (total_blocks < 0)
            return -1;  // Only the first block until the length is known.
    }
    return -1;
}

int blockVictim(const BlockSlot *slots, int count, int64_t read_block)
{
    int behind = -1, past = -1;
    for (int i = 0; i < count; i++) {
        if (slots[i].state == BLOCK_EMPTY)
            return i;
        if (slots[i].state != BLOCK_READY)
            continue;
        int64_t index = slots[i].index;
        if (index < read_block && (behind < 0 || index < slots[behind].index))
            behind = i;
        else if (index >= read_block + BLOCK_AHEAD && (past < 0 || index > slots[past].index))
            past = i;
    }
    return behind >= 0 ? behind : past;
}
