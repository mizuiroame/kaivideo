#include "blockcache.h"

int blockFind(const BlockSlot *slots, int count, int64_t index)
{
    for (int i = 0; i < count; i++)
        if (slots[i].state != BLOCK_EMPTY && slots[i].index == index)
            return i;
    return -1;
}

bool blockIsWanted(int64_t index, const int64_t *readers, int reader_count)
{
    for (int r = 0; r < reader_count; r++)
        if (index >= readers[r] && index < readers[r] + BLOCK_AHEAD)
            return true;
    return false;
}

static bool missing(const BlockSlot *slots, int count, int64_t index, int64_t total_blocks)
{
    return index >= 0 && (total_blocks < 0 || index < total_blocks) && blockFind(slots, count, index) < 0;
}

int64_t blockNextInOrder(const BlockSlot *slots, int count, const int64_t *readers, int reader_count,
                         int64_t total_blocks, int64_t after)
{
    int64_t needed = -1;  // A reader's own block, missing: the one nearest after the last fetched.
    for (int r = 0; r < reader_count; r++) {
        int64_t index = readers[r];
        if (!missing(slots, count, index, total_blocks))
            continue;
        bool nearer = needed < 0 || (index > after && (needed <= after || index < needed)) ||
                      (index <= after && needed <= after && index > needed);
        if (nearer)
            needed = index;
    }
    if (needed >= 0)
        return needed;
    if (after >= 0 && total_blocks >= 0 && blockIsWanted(after + 1, readers, reader_count) &&
        missing(slots, count, after + 1, total_blocks))
        return after + 1;
    return blockNextWanted(slots, count, readers, reader_count, total_blocks);
}

int64_t blockNextWanted(const BlockSlot *slots, int count, const int64_t *readers, int reader_count,
                        int64_t total_blocks)
{
    int window = total_blocks < 0 ? 1 : BLOCK_AHEAD;  // Only the first until the length is known.
    for (int distance = 0; distance < window; distance++)
        for (int r = 0; r < reader_count; r++) {
            int64_t index = readers[r] + distance;
            bool inside = total_blocks < 0 || index < total_blocks;
            if (inside && blockFind(slots, count, index) < 0)
                return index;
        }
    return -1;
}

int blockVictim(const BlockSlot *slots, int count, const int64_t *readers, int reader_count)
{
    int behind = -1, past = -1;
    int64_t first_reader = INT64_MAX;
    for (int r = 0; r < reader_count; r++)
        if (readers[r] < first_reader)
            first_reader = readers[r];
    for (int i = 0; i < count; i++) {
        if (slots[i].state == BLOCK_EMPTY)
            return i;
        if (slots[i].state != BLOCK_READY || blockIsWanted(slots[i].index, readers, reader_count))
            continue;
        int64_t index = slots[i].index;
        if (index < first_reader && (behind < 0 || index < slots[behind].index))
            behind = i;
        else if (index >= first_reader && (past < 0 || index > slots[past].index))
            past = i;
    }
    return behind >= 0 ? behind : past;
}
