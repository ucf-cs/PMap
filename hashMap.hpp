// Uses resizing from "A Lock-Free Wait-Free Hash Table"
// Additional info: http://concurrencyfreaks.blogspot.com/2014/08/a-lock-free-hash-table-by-cliff-click.html

// TODO: Uses persistence from PMwCAS, modified to support non-persitent data structures: https://github.com/Microsoft/pmwcas
// Potentially enhanced using ideas from Reuse, Don't Recycle: https://drops.dagstuhl.de/opus/volltexte/2017/8009/pdf/LIPIcs-DISC-2017-4.pdf
// NOTE: Must run ./configure in the glog folder to build successfully.
// NOTE: Reserves 3 bits to use PMwCAS. Leaves no spare bits.

// TODO: Uses hopscotch hashing to improve table utilization: https://en.wikipedia.org/wiki/Hopscotch_hashing
// Lock-free design https://arxiv.org/pdf/1911.03028.pdf and code https://github.com/DaKellyFella/LockFreeHopscotchHashing/blob/master/src/hash-tables/hsbm_lf.h

// Uses xxhash for fast random hashing: https://github.com/RedSpah/xxhash_cpp

// Rule: Any relocated key must be placed at a later index, but no further than the end of its virtual bucket (neighborhood).
// Rule: Once a key/value has been marked with a sentinel, it can never be overwritten.
// Rule: Values are initially bitmarked if they came from a table migration.
// Rule: Table size must be a power of two.

// Requirement: Contiguous placement of keys and values.
// Requirement: Low memory overhead. Should minimize the use of pointers, auxiliary data structures, etc.
// Requirement: Open addressing.
// Requirement: High average performance over recovery performance.

// Desired: Don't stop the clock during resize. Allow operations to complete mid-resize.
// Desired: Support for fetch_add() and other atomics, not just CAS.
// Desired: Optimizations around power law distribution of key access. (Some keys accessed incredibly frequently, most others almost never)

#ifndef __HASHMAP_H__
#define __HASHMAP_H__

#include <assert.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <utility>

#include "xxhash.hpp"

// Pointer marking.
// Offset can be set from 0-2 to mark different bits.
#define SET_MARK(_p, offset) (void *)((uintptr_t)_p | (uintptr_t)(1 << offset))
#define CLR_MARK(_p, offset) (void *)((uintptr_t)_p & (uintptr_t) ~(1 << offset))
#define IS_MARKED(_p, offset) (void *)((uintptr_t)_p & (uintptr_t)(1 << offset))

// TODO: Will limit the neighborhood distance in hopscotch hashing instead.
#define REPROBE_LIMIT 10

template <class Key, class Value>
class ConcurrentHashMap
{
    // Hash function.
    static size_t hash(Key key)
    {
        Key array[1];
        array[0] = key;
        xxh::hash_t<64> hash = xxh::xxhash<64>(array, 1);
        // TODO: Is this implcit cast safe to do? I know that I'm getting a 32-bit hash back.
        return (size_t)hash;
    }

    // Sentinels.
    // Only work if they key and value are pointer types.
    // Otherwise, these must be reserved and unused by any keys or values.
    // Wildcard match any.
    static void *MATCH_ANY;
    // Wildcard match old.
    static void *NO_MATCH_OLD;
    // Tombstone. Reinsertion is supported, but the key slot is permanently claimed once set (unless relocated by hopscotch hashing?)
    // Also used to represent "empty" slots when used as the key.
    static void *TOMBSTONE;
    // Primed tombstone. Marked to prevent any updates to the location, used for resizing.
    static void *TOMBPRIME;

    // A single table.
    // Multiple tables can exist at a time during resizing.
    class Table
    {
        // The hash map control structure.
        class CHM
        {
            // The number of active resizers.
            // We cap this to prevent too many threads from all allocating replacement tables at once.
            std::atomic<size_t> resizersCount;

            size_t highestBit(size_t val)
            {
                // Subtract 1 so the rightmost position is 0 instead of 1.
                return (sizeof(val) * 8) - __builtin_clz(val | 1) - 1;

                // Slower alternative approach.
                size_t onePos = 0;
                for (size_t i = 0; i <= 8 * sizeof(size_t); i++)
                {
                    // Special case for zero.
                    if (i == 8 * sizeof(size_t))
                    {
                        return 0;
                    }

                    size_t mask = (size_t)1 << ((8 * sizeof(size_t) - 1) - i);
                    if ((val & mask) != 0)
                    {
                        onePos = (8 * sizeof(size_t) - 1) - i;
                        break;
                    }
                }
                return onePos;
            }

            // The next part of the table to copy.
            // Represents "work chunks" claimed by resizers.
            // There is no guarantee that any one thread will actually finish a chunk.
            std::atomic<size_t> copyIdx;
            // The amount of chunks completed.
            // Signals when all resizing is finished.
            std::atomic<size_t> copyDone;

            void copyCheckAndPromote(Table *oldTable, size_t workDone)
            {
                // TODO: Consider removing some of these non-essential variables once we have done more testing.
                assert(&(oldTable->chm) == this);
                size_t oldLen = oldTable->len;
                size_t copyDone = this->copyDone.load();
                assert(copyDone + workDone <= oldLen);
                // If we made at leat once slot unusable, then we did some of the needed copy work.
                if (workDone > 0)
                {
                    this->copyDone.fetch_add(workDone);
                }
                // TODO: Consider recording the last resize here.
                return;
            }

            bool copySlot(size_t idx, Table *oldTable, Table *newTable)
            {
                // A minor optimization to eagerly stop put operations from succeeding by placing a tombstone.
                Key key;
                while ((key = oldTable->key(idx)) == NULL)
                {
                    oldTable->CASkey(idx, NULL, TOMBSTONE);
                }

                // Prevent new values from appearing in the old table.
                // Mark what we see in the old table to prevent future updates.
                Value oldVal = oldTable->value(idx);
                // Keep trying to mark the value until we succeed.
                while (!IS_MARKED(oldVal, 0))
                {
                    // If there isn't a usable value to migrate, replace it with a tombprime. Otherwise, mark it.
                    Value mark = (oldVal == NULL || oldVal == TOMBSTONE) ? TOMBPRIME : SET_MARK(oldVal, 0);
                    // Attempt the CAS.
                    Value actualVal = oldTable->CASvalue(idx, oldVal, mark);
                    // If we succeeded.
                    if (actualVal == oldVal)
                    {
                        // If we replaced an empty spot.
                        if (mark == TOMBPRIME)
                        {
                            // We are already done.
                            return true;
                        }
                        // Update our record of what's in the old table.
                        oldVal = mark;
                        // More work to do outside of the while loop.
                        break;
                    }
                    else
                    {
                        // We failed. Update the old value for CAS and retry.
                        oldVal = oldTable->value(idx);
                    }
                }
                // We have successfully marked the value.

                // If we marked with a tombstone.
                if (oldVal == TOMBPRIME)
                {
                    // No need to migrate a value. We are done.
                    return false;
                }
                // Try to copy the value from the old table into the new table.
                Value oldUnmarked = CLR_MARK(oldVal, 0);
                // If this was a tombstone, whe should have already finished. This should be impossible.
                assert(oldUnmarked != TOMBSTONE);
                // Attempt to copy the value into the new table.
                // Only succeeds if there isn't already a value there.
                // If there is, we say that our write "happened before" the write that placed the existing value.
                bool copiedIntoNew = (putIfMatch(newTable, key, oldUnmarked, NULL) == NULL);

                // Now that the value has been migrated, replace the old table value with a tombstone.
                // This will prevent other threads from redundantly attempting to copy to the new table.
                Value actualVal = oldTable->CASvalue(idx, oldVal, TOMBPRIME);
                while (actualVal != oldVal)
                {
                    oldVal = actualVal;
                    actualVal = oldTable->CASvalue(idx, oldVal, TOMBPRIME);
                }
                // Return whether or not we made progress (copied a value from the old table to the new table).
                return copiedIntoNew;
            }

        public:
            // The number of active KV pairs.
            // If this number gets too large, consider resizing.
            std::atomic<size_t> size;

            // The number of usable slots.
            // If this number gets too large, consider resizing.
            std::atomic<size_t> slots;

            // A new table.
            // All values in the current table must migrate here before deallocating the current table.
            std::atomic<Table *> newTable;
            bool CASNewTable(Table *newTable)
            {
                bool ret;
                Table *oldTable = nullptr;
                while (true)
                {
                    ret = this->newTable.compare_exchange_strong(oldTable, newTable);
                    // If we succeeded here.
                    if (ret ||
                        // If someone else already succeeded here.
                        oldTable != nullptr)
                    {
                        break;
                    }
                }
                return ret;
            }

            CHM(size_t size)
            {
                this->size.store(0);
                slots.store(size);
                resizersCount.store(0);
                newTable.store(nullptr);
                copyIdx.store(0);
                copyDone.store(0);
            }
            // TODO: Remove this if possible.
            CHM()
            {
                CHM(Table::MIN_SIZE);
            }

            // Heuristic to estimate if the table is overfull.
            bool tableFull(size_t reprobeCount, size_t len)
            {
                // A cheap check to potentially avoid the atomic get.
                // Just check how many times we reprobed initially.
                // If we reprobed too far, this suggests an overfull table.
                return reprobeCount >= REPROBE_LIMIT &&
                       // If the table is over 1/4 full.
                       // TODO: Can probably stretch this further with hopscotch hashing.
                       slots.load() >= REPROBE_LIMIT + (len / 4);
            }

            // A wait-free resize.
            Table *resize(Table *table)
            {
                // Check for a resize in progress.
                // If one is found, return the already-existing new table.
                Table *newTable = this->newTable.load();
                if (newTable != nullptr)
                {
                    return newTable;
                }

                // No copy is in progress, so start one.

                // Compute the new table size.
                // Total capacity of the current table.
                size_t oldLen = table->len;
                // Current number of KV pairs stored in the table.
                size_t size = this->size.load();
                // An initial size estimate.
                size_t newSize = size;

                // Heuristic to determine a new size.
                // If we are >25% full of keys.
                if (size >= (oldLen / 4))
                {
                    // Double size.
                    newSize = oldLen << 1;
                }
                // If we are >50% full of keys.
                if (size >= (oldLen / 2))
                {
                    // Quadruple size.
                    newSize = oldLen << 2;
                }

                // TODO: Consider the last resize. If it was recent, then double again.
                // This helps reduce the number of resizes, particularly early on.

                // Disallow shrinking the table.
                if (newSize < oldLen)
                {
                    newSize = oldLen;
                }

                // Compute log2 of newSize.
                size_t log2 = highestBit(newSize);

                // Limit the number of resizers.
                // We do this by "taking a number" to see how many are already working on it.
                size_t r = resizersCount.fetch_add(1);
                // TODO: Wait for a bit if there are at least 2 threads already trying to resize.

                // Check one last time to make sure the table has not yet been allocated.
                newTable = this->newTable.load();
                if (newTable != nullptr)
                {
                    return newTable;
                }

                // Allocate the new table.
                newTable = new Table(newSize);

                // Attempt to CAS the new table.
                // Only one thread can succeed here.
                if (CASNewTable(newTable))
                {
                    // We succeeded.
                }
                else
                {
                    // Failure means some other thread succeeded.
                    // Free the allocated memory.
                    delete newTable;
                    // And get the table that was placed.
                    newTable = this->newTable.load();
                    // The new table should never be NULL.
                    assert(newTable != nullptr);
                }
                return newTable;
            }

            Table *copySlotAndCheck(Table *oldTable, size_t idx, bool shouldHelp)
            {
                assert(&(oldTable->chm) == this);
                Table *newTable = this->newTable.load();
                // Don't bother copying if there isn't even a table transfer in progress.
                assert(newTable != NULL);
                // Copy the desired slot.
                if (copySlot(idx, oldTable, newTable))
                {
                    // Record that a slot was copied.
                    copyCheckAndPromote(oldTable, 1);
                }
                // Help the copy along, unless this was called recursively.
                return shouldHelp ? newTable : helpCopy(newTable);
            }

            // Help migrate the table.
            // Do not migrate the whole table by default.
            void helpCopyImpl(Table *oldTable, bool copyAll = false)
            {
                assert(&(oldTable->chm) == this);
                Table *newTable = this->newTable.load();
                assert(newTable != NULL);
                size_t oldLen = oldTable->len;
                const size_t MIN_COPY_WORK = (oldLen < 1024) ? oldLen : 1024;

                long panicStart = -1;
                size_t copyIdx;

                // If copying is not yet complete.
                while (copyDone.load() < oldLen)
                {
                    // If we have not yet paniced.
                    if (panicStart == -1)
                    {
                        // Try to claim a chunk of work.
                        copyIdx = this->copyIdx.load();
                        while (copyIdx < (oldLen << 1) &&
                               !this->copyIdx.compare_exchange_strong(copyIdx, copyIdx + MIN_COPY_WORK))
                        {
                            copyIdx = this->copyIdx.load();
                        }
                        // Panic if the threads have collectively attempted to copy the table twice, yet the work still isn't done.
                        if (copyIdx >= (oldLen << 1))
                        {
                            // Record where the panic occurred.
                            panicStart = copyIdx;
                        }
                    }

                    // Now that we have claimed a chunk of work, work on it.
                    size_t workDone = 0;
                    // For each index in our chunk.
                    for (size_t i = 0; i < MIN_COPY_WORK; i++)
                    {
                        // Copy from the old table to the new table.
                        // If we successfully modified the old slot to disallow key replacement.
                        if (copySlot((copyIdx + i) & (oldLen - 1), oldTable, newTable))
                        {
                            // Count it.
                            workDone++;
                        }
                    }
                    // If we got *something* done.
                    if (workDone > 0)
                    {
                        // Tell the other threads about it.
                        copyCheckAndPromote(oldTable, workDone);
                    }
                    // Move on to the next chunk of work.
                    copyIdx += MIN_COPY_WORK;
                    // Stop working after just doing the bare minimum amount of work.
                    // NOTE: This can be commented out to instead keep taking on additional chunks of work until the whole resize process is complete.
                    if (!copyAll && panicStart == -1)
                    {
                        return;
                    }
                }
                // An extra promotion check in case some thread got stalled while promoting.
                // TODO: Really a waste if we have confidence that our asserts will succeed.
                copyCheckAndPromote(oldTable, 0);
                return;
            }
        };
        typedef struct KVpair
        {
            std::atomic<Key> key;
            std::atomic<Value> value;
        } KVpair;
        // NOTE: Hashes are only needed if pointer comparison is insuccicient for comparison, so we don't use it in this implementation.
        // Keys and values.
        KVpair *pairs;

    public:
        // Minimum table size.
        // Must always be a power of two.
        const static size_t MIN_SIZE = 1 << 3;

        // CHM: Hash Table Control Structure.
        CHM chm;
        // The number of pairs that can fit in the table.
        size_t len;

        Table(size_t size)
        {
            assert(size % 2 == 0);
            assert(size >= MIN_SIZE);
            new (&chm) CHM(size);
            pairs = new KVpair[size];
            for (size_t i = 0; i < size; i++)
            {
                pairs[i].key.store(TOMBSTONE);
                pairs[i].value.store(TOMBSTONE);
            }
            len = size;
            return;
        }
        ~Table()
        {
            delete[] pairs;
            return;
        }
        // Function to get a key at an index.
        Key key(size_t idx)
        {
            assert(idx < len);
            return pairs[idx].key.load();
        }
        // Function to get a value at an index.
        Value value(size_t idx)
        {
            assert(idx < len);
            return pairs[idx].value.load();
        }
        // Function to CAS a key.
        Key CASkey(size_t idx, Key oldKey, Key newKey)
        {
            assert(idx < len);
            Key oldKeyRef = oldKey;
            pairs[idx].key.compare_exchange_strong(oldKeyRef, newKey);
            return oldKeyRef;
        }
        // Function to CAS a value.
        Value CASvalue(size_t idx, Value oldValue, Value newValue)
        {
            assert(idx < len);
            Key oldValueRef = oldValue;
            pairs[idx].value.compare_exchange_strong(oldValueRef, newValue);
            return oldValueRef;
        }
    };

    // The structure that stores the top table.
    std::atomic<Table *> table;

    // Heuristics for resizing.
    // Consider a reprobes heuristic, or some alternative, to indicate when to resize and gather statistics on key distribution.
    // TODO: Reprobes are a bit different with hopscotch hashing.
    static size_t reprobeLimit(size_t len)
    {
        return REPROBE_LIMIT + (len >> 2);
    }

public:
    // Constructors.
    ConcurrentHashMap()
    {
        ConcurrentHashMap(Table::MIN_SIZE);
        return;
    }
    ConcurrentHashMap(size_t size)
    {
        table.store(new Table(size));
        return;
    }

    // This number is really only meaningful if the size is not being changed by other threads.
    size_t size()
    {
        return table->chm.size();
    }

    bool isEmpty()
    {
        return size() == 0;
    }

    bool containsKey(Key key)
    {
        return (get(key) != NULL);
    }

    // bool contains(Value value)
    // {
    //     return containsValue(value);
    // }

    Value put(Key key, Value value)
    {
        return putIfMatch(key, value, NO_MATCH_OLD);
    }

    Value putIfAbsent(Key key, Value value)
    {
        return putIfMatch(key, value, TOMBSTONE);
    }

    bool remove(Key key)
    {
        return putIfMatch(key, TOMBSTONE, NO_MATCH_OLD);
    }

    // Remove key with specified value.
    bool remove(Key key, Value value)
    {
        return putIfMatch(key, TOMBSTONE, value) == value;
    }

    Value replace(Key key, Value oldValue, Value newValue)
    {
        return putIfMatch(key, newValue, oldValue) == oldValue;
    }

    // TODO: Implement an update method.

    Value putIfMatch(Key key, Value newVal, Value oldVal)
    {
        assert(newVal != NULL);
        assert(oldVal != NULL);
        Value retVal = putIfMatch(table, key, newVal, oldVal);
        assert(!IS_MARKED(retVal, 0));
        assert(retVal != NULL);
        return retVal == TOMBSTONE ? NULL : retVal;
    }

    // NOTE: The following are nonessential functions, in my mind.
    // putAll. Copies all values from one map into another. May not be safe in a concurrent context.
    // clear: Empty the table.
    // containsValue: Check for the existance of a value accross all keys.

    // Check for key equality.
    // We could make different versions of this for different key types.
    static bool keyEq(Key K, Key key)
    {
        // Keys match exactly.
        return K == key;
    }

    Value getImpl(Table *table, Key key, int fullHash)
    {
        // The capacity of the table.
        size_t len = table->len;

        // The hash of the key.
        // Truncated to keep within the boundaries of the key range.
        size_t idx = fullHash & (len - 1);

        // Probe loop.
        // Keep searching until the key is found or we have exceeded the probe bounds.
        // TODO: Modify this to support hopscotch hashing.
        size_t reprobeCount = 0;
        while (true)
        {
            // Probe the table.
            // NOTE: These are atomic reads. We must carefully adjust this if we want to support relocating keys.
            Key K = table->key(idx);
            Value V = table->value(idx);

            // The key was not present.
            if (K == NULL)
            {
                return NULL;
            }

            // Check for the existance of a new table.
            Table *newTable = table->chm.newTable.load();

            // Compare the key we found.
            // We do this because multiple keys can hash to the same index.
            if (keyEq(K, key))
            {
                // We found the target key.

                // Check to make sure there isn't a table copy in progress.
                if (!IS_MARKED(V, 0))
                {
                    // No table copy.
                    // We can return the assoicated value.
                    return (V == TOMBSTONE) ? NULL : V;
                }

                // Key may only be partially copied.
                // Finish the copy and retry.
                return getImpl(table->chm.copySlotAndCheck(table, idx, key == NULL), key, fullHash);
            }

            // If we have exceeded our reprobe limit.
            if (++reprobeCount >= reprobeLimit(len) ||
                // Or if we found a tombstone key, indicating there are no more keys in this table.
                K == TOMBSTONE)
            {
                // Retry in the new table.
                return (newTable == NULL) ? NULL : getImpl(helpCopy(newTable), key, fullHash);
            }

            // Probe to the next index.
            idx = (idx + 1) & (len - 1);
        }
    }

    // Get the value associated with a particular key.
    Value get(Key key)
    {
        size_t fullhash = hash(key);
        Value V = getImpl(table, key, fullhash);
        assert(!IS_MARKED(V, 0));
        return V;
    }

    // Called by most put functions. This one does the heavy lifting.
    static Value putIfMatch(Table *table, Key key, Value newVal, Value oldVal)
    {
        assert(newVal != NULL);
        assert(!IS_MARKED(newVal, 0));
        assert(!IS_MARKED(oldVal, 0));
        size_t len = table->len;
        size_t idx = hash(key) & (len - 1);

        size_t reprobeCount = 0;
        Key K;
        Value V;
        Table *newTable = NULL;
        // Spin until we get a key slot.
        while (true)
        {
            // Get the key and value in the current slot.
            Key K = table->key(idx);
            Value V = table->value(idx);

            // If the slot is free.
            if (K == NULL)
            {
                // If we find an empty slot, the key was never in the table.

                // If we were trying to remove the key.
                if (newVal == ConcurrentHashMap<Key, Value>::TOMBSTONE)
                {
                    // We don't need to do anything.
                    return newVal;
                }

                // Claim the NULL key slot.
                Key actualKey = table->CASkey(idx, NULL, key);
                // If the CAS succeeded.
                if (actualKey == NULL)
                {
                    table->chm.slots.fetch_add(1);
                    break;
                }
                // CAS failed. Try again.
                else
                {
                    // Update the expected key with what was actually found during the CAS.
                    K = actualKey;
                }
            }
            // The slot is not empty.

            newTable = table->chm.newTable.load();

            // See if we found a match.
            if (keyEq(K, key))
            {
                break;
            }

            // If we probe too far.
            if (++reprobeCount >= reprobeLimit(len) ||
                // Or if we run out of space.
                K == TOMBSTONE)
            {
                // Resize the table.
                newTable = table->chm.resize(table);
                // Help copy over an existing value.
                // If we are attempting to replace the value without concern for the old value, we don't have to bother with this.
                // In practice, we only ignore this within an existing migration.
                if (oldVal != NULL)
                {
                    helpCopy(newTable);
                }
                // Try again in the new table.
                return putIfMatch(newTable, key, newVal, oldVal);
            }
            // Reprobe.
            idx = (idx + 1) & (len - 1);
        }
        // Now we have a key slot.

        // If the value we want to place is already there.
        if (newVal == V)
        {
            // Then we can get away with doing nothing.
            return V;
        }

        // Consider allocating a newer table for placement.
        // If a new table hasn't already been allocated.
        if (newTable == NULL &&
            // And we are trying to remove the value and the table is full.
            ((V == NULL && table->chm.tableFull(reprobeCount, len)) ||
             // Or our value is primed.
             IS_MARKED(V, 0)))
        {
            // Force the table copy to start.
            newTable = table->chm.resize(table);
        }

        // If a new table is allocated.
        if (newTable != NULL)
        {
            // Copy the slot and retry in the new table.
            return putIfMatch(table->chm.copySlotAndCheck(table, idx, oldVal == NULL), key, newVal, oldVal);
        }

        // Update the existing table.
        while (true)
        {
            assert(!IS_MARKED(V, 0));

            // May want to quit early if the slot doesn't contain the expected value.

            // If we aren't doing a wildcard match.
            if (oldVal != NO_MATCH_OLD &&
                // And the expected value doesn't match.
                V != oldVal &&
                // And either:
                // We are not looking for a "real" old value.
                // The current value is a tombstone.
                // The current value is NULL.
                // (Essentially, any non-value match)
                (oldVal != MATCH_ANY || V == TOMBSTONE || V == NULL) &&
                // And either the value we wish to assign isn't NULL or we aren't expecting a tombstone
                (V != NULL || oldVal != TOMBSTONE))
            {
                // Don't bother updating the table.
                return V;
            }

            // Atomically update the value.
            Value actualValue = table->CASvalue(idx, V, newVal);
            // If we succeeded.
            if (actualValue == V)
            {
                // Adjust the size counters for the table.
                // If this was a table copy, do nothing.
                if (oldVal != NULL)
                {
                    // If we removed a NULL or tombstone value with a non-tombstone.
                    if ((V == NULL || V == TOMBSTONE) && newVal != TOMBSTONE)
                    {
                        table->chm.size.fetch_add(1);
                    }
                    // If we removed a non-NULL and non-tombstone value with a tombstone.
                    if (!(V == NULL || V == TOMBSTONE) && newVal == TOMBSTONE)
                    {
                        table->chm.size.fetch_add(-1);
                    }
                }
                // If we replaced a NULL value when we had expected a non-NULL value, return the tombstone sentinel.
                return (V == NULL && oldVal != NULL) ? TOMBSTONE : V;
            }
            // CAS failed.
            else
            {

                // Update the value with what we found during the failed CAS.
                V = actualValue;
            }
            // If a primed value was placed, re-run put on the new table.
            if (IS_MARKED(V, 0))
            {
                return putIfMatch(table->chm.copySlotAndCheck(table, idx, oldVal == NULL), key, newVal, oldVal);
            }
        }
    }

    // Help to perform table migration, likely being assigned some range of values.
    // TODO: I have decided to assume the helper is always the top level table. This may not always be true.
    static Table *helpCopy(Table *helper)
    {
        Table *topTable = helper; //table.load();

        // If there is no copy in progress, then there's nothing to be done here.
        if (topTable->chm.newTable.load() == nullptr)
        {
            return helper;
        }
        topTable->chm.helpCopyImpl(topTable, false);
        return helper;
    }
};

template <typename Key, typename Value>
void *ConcurrentHashMap<Key, Value>::MATCH_ANY = new char();
template <typename Key, typename Value>
void *ConcurrentHashMap<Key, Value>::NO_MATCH_OLD = new char();
template <typename Key, typename Value>
void *ConcurrentHashMap<Key, Value>::TOMBSTONE = new char();
template <typename Key, typename Value>
void *ConcurrentHashMap<Key, Value>::TOMBPRIME = SET_MARK(TOMBSTONE, 0);

#endif