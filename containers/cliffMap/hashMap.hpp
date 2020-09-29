// Uses resizing from "A Lock-Free Wait-Free Hash Table"
// Additional info: http://concurrencyfreaks.blogspot.com/2014/08/a-lock-free-hash-table-by-cliff-click.html

// TODO: Uses persistence from PMwCAS, modified to support non-persitent data structures: https://github.com/Microsoft/pmwcas
// Potentially enhanced using ideas from Reuse, Don't Recycle: https://drops.dagstuhl.de/opus/volltexte/2017/8009/pdf/LIPIcs-DISC-2017-4.pdf
// NOTE: Must run ./configure in the glog folder to build theirs successfully.
// NOTE: Reserves 3 bits to use PMwCAS. Leaves no spare bits.
// Easy persistence can be done based on this: https://dl.acm.org/doi/abs/10.1145/2935764.2935810 and this: http://concurrencyfreaks.blogspot.com/2018/01/a-lock-free-persistent-queue.html

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

#ifndef HASH_MAP_HPP
#define HASH_MAP_HPP

#include <assert.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <utility>

// Fast hashing library.
#include "xxhash.hpp"
// Persistence functions.
#include "persistence.hpp"
// Pointer marking functions and flags.
#include "marking.hpp"

#include "define.hpp"

// mmap.
#include <sys/mman.h>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>

// These are used to enable and disable different variants of our design.
// TODO: Debug resizing.
//#define RESIZE

// TODO: Will limit the neighborhood distance in hopscotch hashing instead.
inline const size_t REPROBE_LIMIT = 10;

template <class Key, class Value, class Hash = std::hash<Key>>
class ConcurrentHashMap
{
    // Sentinels.
    // Otherwise, these must be reserved and unused by any keys or values.
    // Default values. Indicate that nothing has been placed in this part of the table yet.
    static Key KINITIAL;
    static Value VINITIAL;
    // Wildcard match any.
    static Value MATCH_ANY;
    // Wildcard match old.
    static Value NO_MATCH_OLD;
    // Tombstone. Reinsertion is supported, but the key slot is permanently claimed once set (unless relocated by hopscotch hashing?)
    static Value VTOMBSTONE;
    // Tombstone used to represent emptied slots.
    static Key KTOMBSTONE;
    // Primed tombstone. Marked to prevent any updates to the location, used for resizing.
    static Value TOMBPRIME;

    // Heuristics for resizing.
    // Consider a reprobes heuristic, or some alternative, to indicate when to resize and gather statistics on key distribution.
    // TODO: Reprobes are a bit different with hopscotch hashing.
    static size_t reprobeLimit(size_t len)
    {
        return REPROBE_LIMIT + (len >> 2);
    }

public:
    // A key value pair.
    // Using this struct enables adjacent placement of the keys and values in memory.
    typedef struct KVpair
    {
        std::atomic<Key> key;
        std::atomic<Value> value;
    } KVpair;
    // A single table.
    // Multiple tables can exist at a time during resizing.
    class Table
    {
    public:
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

#ifdef RESIZE
            // The next part of the table to copy.
            // Represents "work chunks" claimed by resizers.
            // There is no guarantee that any one thread will actually finish a chunk.
            std::atomic<size_t> copyIdx;
            // The amount of chunks completed.
            // Signals when all resizing is finished.
            std::atomic<size_t> copyDone;

            void copyCheckAndPromote(ConcurrentHashMap<Key, Value, Hash> *hashMap, Table *oldTable, size_t workDone)
            {
                assert(&(oldTable->chm) == this);
                size_t oldLen = oldTable->len;
                size_t copyDone = this->copyDone.load();
                assert(copyDone + workDone <= oldLen);
                // If we made at leat once slot unusable, then we did some of the needed copy work.
                if (workDone > 0)
                {
                    this->copyDone.fetch_add(workDone);
                }
                // Attempt table promotion.
                if (copyDone + workDone == oldLen &&
                    hashMap->table.compare_exchange_strong(oldTable, newTable))
                {
                    // TODO: Record the last resize here.
                    //hashMap->lastResize = std::chrono::high_resolution_clock::now();

                    // TODO: Determine when it is safe to deallocate the old table(s).
                }
                return;
            }

            bool copySlot(ConcurrentHashMap<Key, Value, Hash> *hashMap, size_t idx, Table *oldTable, Table *newTable)
            {
                // A minor optimization to eagerly stop put operations from succeeding by placing a tombstone.
                Key key;
                while ((key = oldTable->key(idx)) == KINITIAL)
                {
                    oldTable->CASkey(idx, KINITIAL, KTOMBSTONE);
                }

                // Prevent new values from appearing in the old table.
                // Mark what we see in the old table to prevent future updates.
                Value oldVal = oldTable->value(idx);
                // Keep trying to mark the value until we succeed.
                while (!isMarked((uintptr_t)oldVal, MigrationFlag))
                {
                    // If there isn't a usable value to migrate, replace it with a tombprime. Otherwise, mark it.
                    Value mark = (oldVal == VINITIAL || oldVal == VTOMBSTONE) ? TOMBPRIME : (Value)setMark((uintptr_t)oldVal, MigrationFlag);
                    // Attempt the CAS.
                    Value actualVal = CASvalue(oldTable, idx, oldVal, mark);
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
                Value oldUnmarked = (Value)clearMark((uintptr_t)oldVal, MigrationFlag);
                // If this was a tombstone, whe should have already finished. This should be impossible.
                assert(oldUnmarked != VTOMBSTONE);
                // Attempt to copy the value into the new table.
                // Only succeeds if there isn't already a value there.
                // If there is, we say that our write "happened before" the write that placed the existing value.
                bool copiedIntoNew = (hashMap->putIfMatch(newTable, key, oldUnmarked, VINITIAL) == VINITIAL);

                // Now that the value has been migrated, replace the old table value with a tombstone.
                // This will prevent other threads from redundantly attempting to copy to the new table.
                Value actualVal = CASvalue(oldTable, idx, oldVal, TOMBPRIME);
                while (actualVal != oldVal)
                {
                    oldVal = actualVal;
                    actualVal = CASvalue(oldTable, idx, oldVal, TOMBPRIME);
                }
                // Return whether or not we made progress (copied a value from the old table to the new table).
                return copiedIntoNew;
            }
#endif
        public:
            // The number of active KV pairs.
            // If this number gets too large, consider resizing.
            std::atomic<size_t> size;

            // The number of usable slots.
            // If this number gets too large, consider resizing.
            std::atomic<size_t> slots;
#ifdef RESIZE
            // A table.
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
#endif
            CHM(size_t tableCapacity = Table::MIN_SIZE, size_t existingSize = 0)
            {
                this->size.store(existingSize);
                slots.store(tableCapacity);
#ifdef RESIZE
                resizersCount.store(0);
                newTable.store(nullptr);
                copyIdx.store(0);
                copyDone.store(0);
#endif
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
#ifdef RESIZE
            // A wait-free resize.
            Table *resize(ConcurrentHashMap<Key, Value, Hash> *hashMap, Table *table)
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
                    //newSize = oldLen;
                    // Always enforce a larger table upon resize.
                    // For some reason, without this, we are getting stuck in a loop of resizing (to the same size) then failing to insert, repeating in a vicious cycle.
                    newSize = oldLen * 2;
                }

                // Compute log2 of newSize.
                size_t log2 = highestBit(newSize);

                // Limit the number of resizers.
                // We do this by "taking a number" to see how many are already working on it.
                // size_t r = resizersCount.fetch_add(1);
                // TODO: Wait for a bit if there are at least 2 threads already trying to resize.

                // Check one last time to make sure the table has not yet been allocated.
                newTable = this->newTable.load();
                if (newTable != nullptr)
                {
                    return newTable;
                }

                // Allocate the new table.
                // TODO: We need to mmap the table and pass in the address space here so we know where to place pairs.
                newTable = new Table(newSize, size);

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

            Table *copySlotAndCheck(ConcurrentHashMap<Key, Value, Hash> *hashMap, Table *oldTable, size_t idx, bool shouldHelp)
            {
                assert(&(oldTable->chm) == this);
                Table *newTable = this->newTable.load();
                // Don't bother copying if there isn't even a table transfer in progress.
                assert(newTable != nullptr);
                // Copy the desired slot.
                if (copySlot(hashMap, idx, oldTable, newTable))
                {
                    // Record that a slot was copied.
                    copyCheckAndPromote(hashMap, oldTable, 1);
                }
                // Help the copy along, unless this was called recursively.
                return shouldHelp ? newTable : hashMap->helpCopy(newTable);
            }

            // Help migrate the table.
            // Do not migrate the whole table by default.
            void helpCopyImpl(ConcurrentHashMap<Key, Value, Hash> *hashMap, Table *oldTable, bool copyAll = false)
            {
                assert(&(oldTable->chm) == this);
                Table *newTable = this->newTable.load();
                assert(newTable != nullptr);
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
                        if (copySlot(hashMap, (copyIdx + i) & (oldLen - 1), oldTable, newTable))
                        {
                            // Count it.
                            workDone++;
                        }
                    }
                    // If we got *something* done.
                    if (workDone > 0)
                    {
                        // Tell the other threads about it.
                        copyCheckAndPromote(hashMap, oldTable, workDone);
                    }
                    // Move on to the next chunk of work.
                    copyIdx += MIN_COPY_WORK;
                    // Stop working after just doing the bare minimum amount of work.
                    // NOTE: This can be commented out to instead keep taking on additional chunks of work until the whole resize process is complete.
                    // if (!copyAll && panicStart == -1)
                    // {
                    //     return;
                    // }
                }
                // Try to promote the hashtable anyway, in case another thread stalled during the promotion phase.
                copyCheckAndPromote(hashMap, oldTable, 0);
                return;
            }
#endif
        };
        // NOTE: Hashes are only needed if pointer comparison is insufficient for comparison, so we don't use it in this implementation.
        // Keys and values.
        KVpair *pairs;

        // Minimum table size.
        // Must always be a power of two.
        const static size_t MIN_SIZE = 1 << 3;

        // CHM: Hash Table Control Structure.
        CHM chm;
        // The number of pairs that can fit in the table.
        size_t len;

        Table(size_t tableCapacity, size_t existingSize, KVpair *pairs = NULL)
        {
            assert(tableCapacity % 2 == 0);
            assert(tableCapacity >= MIN_SIZE);
            new (&chm) CHM(tableCapacity, existingSize);
            assert(pairs != NULL);
            this->pairs = pairs;
            len = tableCapacity;
            return;
        }
        ~Table()
        {
            return;
        }
        // Function to get a key at an index.
        Key key(size_t idx)
        {
            assert(idx < len);
            Key ret = pcas_read<Key>(&pairs[idx].key);
            return ret;
        }
        // Function to get a value at an index.
        Value value(size_t idx)
        {
            assert(idx < len);
            Value ret = pcas_read<Value>(&pairs[idx].value);
            return ret;
        }
        // Function to CAS a key.
        Key CASkey(size_t idx, Key oldKey, Key newKey)
        {
            assert(idx < len);
            Key oldKeyRef = oldKey;
            pcas<Key>(&pairs[idx].key, oldKeyRef, newKey);
            return oldKeyRef;
        }
        // Function to CAS a value.
        static Value CASvalue(Table *table, size_t idx, Value oldValue, Value newValue)
        {
            assert(idx < table->len);
            Value oldValueRef = oldValue;
            pcas<Value>(&table->pairs[idx].value, oldValueRef, newValue);
            return oldValueRef;
        }
        // Function to increment a value.
        static Value increment(Table *table, size_t idx, Value oldValue, Value newValue)
        {
            assert(idx < table->len);
            Key oldValueRef = oldValue;
            if (oldValue == VINITIAL || oldValue == VTOMBSTONE)
            {
                oldValue = 0;
            }
            // Our actual new value here is dependent on the old value.
            // NOTE: The correct way to perform this addition is entirely dependent on the type of Value.
            newValue = ((oldValue >> 3) + 1) << 3;
            // Must be CAS rather than FAA because the old value might be a sentinel.
            pcas<Value>(&table->pairs[idx].value, oldValueRef, newValue);
            return oldValueRef;
        }
    };

    // Constructor.
    ConcurrentHashMap(const char *fileName, size_t size = Table::MIN_SIZE, bool reconstruct = true)
    {
        // NOTE: Without resizing, we can just grab the static size of the table for recovery. size is the number of KVPairs.

        // TODO: Memory mapping isn't as simple if we have to handle multiple tables.
        // This will hold the file descriptor of our memory mapped file.
        int fd;
        // This will hold the memory address of our memory mapped table.
        void *address;
        // A temporary place to reference the table.
        Table *table;
        if (reconstruct)
        {
            // Try to open an existing hash table.
            fd = open(fileName, O_RDWR);
        }
        else
        {
            // Report table doesn't exist.
            fd = -1;
        }

        // If we succeeded, just map the existing data.
        if (fd != -1)
        {
            // // Used to store file information.
            // struct stat finfo;
            // // Get existing file size.
            // if (fstat(fd, &finfo) == -1)
            // {
            //     // error
            //     fprintf(stderr, "Failed to read the existing file's size.\n");
            // }
            // size_t length = finfo.st_size;

            // We are getting the fixed size for now.
            // TODO: Support resizing later.
            size_t length = sizeof(KVpair) * size;
            // Map the file.
            address = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if ((intptr_t)address == -1)
            {
                // error
                std::cerr << "Failed to mmap the existing file. errno = "
                          << errno << ", " << strerror(errno) << std::endl;
                throw std::logic_error("mmap existing file failed.");
            }

            // Allocate our table.
            // We pass in the asigned location of our KV pairs to assign them to the structure.
            // We pass in the size of the table to assign the length.
            table = new Table(size, 0, (KVpair *)address);

            // Use the KV pairs to infer the number of used and free entries in the table.
            table->chm.size.store(0);
            table->chm.slots.store(0);
            for (size_t i = 0; i < size; i++)
            {
                Value V = ((Table *)address)->pairs[i].value;
                // Anything that's not a sentinel.
                if (V != VINITIAL && V != VTOMBSTONE && V != TOMBPRIME)
                {
                    table->chm.size.fetch_add(1);
                }
                // Anything left that's not a tombstone.
                else if (V != VTOMBSTONE && V != TOMBPRIME)
                {
                    table->chm.slots.fetch_add(1);
                }
            }
        }
        // If file doesn't exist yet. Try to make it.
        else
        {
            // Create and open the file.
            fd = open(fileName, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
            if (fd == -1)
            {
                // error
                std::cerr << "Failed to create or open the file." << std::endl;
                throw std::runtime_error("cannot create or open file");
            }
            // Allocate enough space for the KV pairs.
            size_t length = sizeof(KVpair) * size;
            // Truncate will actually extend the size of the file by filling with NULL.
            if (ftruncate(fd, length) == -1)
            {
                // error
                std::cerr << "Failed to adjust file size." << std::endl;
                throw std::runtime_error("cannot create or open file");
            }
            // Allocate our file.
            KVpair *pairs = (KVpair *)mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if ((intptr_t)pairs == -1)
            {
                // error
                std::cerr << "Failed to mmap a new file. errno = "
                          << errno << ", " << strerror(errno) << std::endl;
                throw std::logic_error("mmap new file failed.");
            }
            // Initialize the new file.
            for (size_t i = 0; i < size; i++)
            {
                // Initialize these to a default, reserved value.
                pairs[i].key.store((Key)setMark(KINITIAL, DirtyFlag));
                pairs[i].value.store((Value)setMark(VINITIAL, DirtyFlag));
            }
            // Persist all keys and values.
            // Everything else can be inferred upon recovery.
            PERSIST(pairs, sizeof(KVpair *) * size);
            // Allocate our table.
            // We pass in the location of our KV pairs to assign them to the structure.
            // We pass in the size of the table to assign the length.
            table = new Table(size, 0, pairs);
        }
        // After the mmap() call has returned, the file descriptor, fd, can be closed immediately, without invalidating the mapping.
        close(fd);
        // Store the table.
        this->table.store(table);
        return;
    }
    ConcurrentHashMap(size_t sz = Table::MIN_SIZE)
        : ConcurrentHashMap("./hashmap.dat", sz)
    {
    }
    ~ConcurrentHashMap()
    {
        size_t size = table.load()->chm.size.load();
        // Unmap the file.
        if (munmap(table.load(), sizeof(Table) + (sizeof(KVpair) * size)) != 0)
        {
            // error
            fprintf(stderr, "Failed to unmap the file from memory.\n");
        }
        return;
    }

    // This number is really only meaningful if the size is not being changed by other threads.
    size_t size()
    {
        return table.load()->chm.size.load();
    }
    bool isEmpty()
    {
        return size() == 0;
    }
    bool containsKey(Key key)
    {
        return (get(key) != KINITIAL);
    }
    Value put(Key key, Value value)
    {
        return putIfMatch(key, value, NO_MATCH_OLD);
    }
    Value putIfAbsent(Key key, Value value)
    {
        return putIfMatch(key, value, VTOMBSTONE);
    }
    bool remove(Key key)
    {
        return putIfMatch(key, VTOMBSTONE, NO_MATCH_OLD);
    }
    // Remove key with specified value.
    bool remove(Key key, Value value)
    {
        return putIfMatch(key, VTOMBSTONE, value) == value;
    }
    bool replace(Key key, Value oldValue, Value newValue)
    {
        return (putIfMatch(key, newValue, oldValue) == oldValue);
    }
    // Accept an arbitrary function to replace the use of standard CAS.
    // Enables more complex logic by allowing the new value to adapt based on the actual old value.
    Value update(Key key, Value value, Value function(Table *table, size_t idx, Value oldValue, Value newValue))
    {
        return putIfMatch(key, value, NO_MATCH_OLD, function);
    }

    Value putIfMatch(Key key, Value newVal, Value oldVal, Value CAS(Table *table, size_t idx, Value oldValue, Value newValue) = &Table::CASvalue)
    {
        assert(newVal != VINITIAL);
        assert(oldVal != VINITIAL);
        Value retVal = putIfMatch(table.load(), key, newVal, oldVal, CAS);
        assert(!isMarked(retVal, MigrationFlag));
        return retVal == VTOMBSTONE ? VINITIAL : retVal;
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
            if (K == KINITIAL)
            {
                return VINITIAL;
            }
#ifdef RESIZE
            // Check for the existance of a new table.
            Table *newTable = table->chm.newTable.load();
#endif
            // Compare the key we found.
            // We do this because multiple keys can hash to the same index.
            if (keyEq(K, key))
            {
                // We found the target key.
#ifdef RESIZE
                // Check to make sure there isn't a table copy in progress.
                if (!isMarked((uintptr_t)V, MigrationFlag))
                {
                    // No table copy.
                    // We can return the assoicated value.
                    return (V == VTOMBSTONE) ? VINITIAL : V;
                }

                // Key may only be partially copied.
                // Finish the copy and retry.
                return getImpl(table->chm.copySlotAndCheck(this, table, idx, key == KINITIAL), key, fullHash);
#else
                return (V == VTOMBSTONE) ? VINITIAL : V;
#endif
            }

            // If we have exceeded our reprobe limit.
            if (++reprobeCount >= reprobeLimit(len) ||
                // Or if we found a tombstone key, indicating there are no more keys in this table.
                K == KTOMBSTONE)
            {
#ifdef RESIZE
                return (newTable == nullptr) ? VINITIAL : getImpl(helpCopy(newTable), key, fullHash);
#else
                // Value is not present.
                return VINITIAL;
#endif
            }

            // Probe to the next index.
            idx = (idx + 1) & (len - 1);
        }
    }

    // Get the value associated with a particular key.
    Value get(Key key)
    {
        size_t fullhash = Hash{}(key);
        Value V = getImpl(table, key, fullhash);
        assert(!isMarked((uintptr_t)V, MigrationFlag));
        return V;
    }

    // Called by most put functions. This one does the heavy lifting.
    Value putIfMatch(Table *table, Key key, Value newVal, Value oldVal,
                     Value CAS(Table *table, size_t idx, Value oldValue, Value newValue) = &Table::CASvalue)
    {
        assert(newVal != VINITIAL);
        assert(!isMarked(newVal, MigrationFlag));
        assert(!isMarked(oldVal, MigrationFlag));
        size_t len = table->len;
        size_t idx = Hash{}(key) & (len - 1);

        size_t reprobeCount = 0;
        Key K;
        Value V;
#ifdef RESIZE
        Table *newTable = nullptr;
#endif
        // Spin until we get a key slot.
        while (true)
        {
            // Get the key and value in the current slot.
            K = table->key(idx);
            V = table->value(idx);

            // If the slot is free.
            if (K == VINITIAL)
            {
                // If we find an empty slot, the key was never in the table.

                // If we were trying to remove the key.
                if (newVal == ConcurrentHashMap<Key, Value, Hash>::VTOMBSTONE)
                {
                    // We don't need to do anything.
                    return newVal;
                }

                // Claim the unused key slot.
                Key actualKey = table->CASkey(idx, KINITIAL, key);
                // If the CAS succeeded.
                if (actualKey == KINITIAL)
                {
                    table->chm.slots.fetch_add(1);
                    break;
                }
                // CAS failed. May need to try again.
                else
                {
                    // Update the expected key with what was actually found during the CAS.
                    K = actualKey;
                }
            }
            // The slot is not empty.

#ifdef RESIZE
            newTable = table->chm.newTable.load();
#endif

            // See if we found a match.
            if (keyEq(K, key))
            {
                break;
            }

            // If we probe too far.
            if (++reprobeCount >= reprobeLimit(len) ||
                // Or if we run out of space.
                K == KTOMBSTONE)
            {
#ifdef RESIZE
                // Resize the table.
                newTable = table->chm.resize(this, table);
                // Help copy over an existing value.
                // If we are attempting to replace the value without concern for the old value, we don't have to bother with this.
                // In practice, we only ignore this within an existing migration.
                if (oldVal != VINITIAL)
                {
                    helpCopy(newTable);
                }
                // Try again in the new table.
                return putIfMatch(newTable, key, newVal, oldVal);
#else
                // The key is not present.
                return VINITIAL;
#endif
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

#ifdef RESIZE
        // Consider allocating a newer table for placement.
        // If a new table hasn't already been allocated.
        if (newTable == nullptr &&
            // And we are doing a fresh key insert while the table is nearly full.
            ((V == VINITIAL && table->chm.tableFull(reprobeCount, len)) ||
             // Or our value is primed.
             isMarked((uintptr_t)V, MigrationFlag)))
        {
            // Force the table copy to start.
            newTable = table->chm.resize(this, table);
        }

        // If a new table is allocated.
        if (newTable != nullptr)
        {
            // Copy the slot and retry in the new table.
            return this->putIfMatch(table->chm.copySlotAndCheck(this, table, idx, oldVal == VINITIAL), key, newVal, oldVal);
        }
#endif
        // Update the existing table.
        while (true)
        {
            assert(!isMarked((uintptr_t)V, MigrationFlag));

            // May want to quit early if the slot doesn't contain the expected value.

            // If we aren't doing a wildcard match.
            if (oldVal != NO_MATCH_OLD &&
                // And the expected value doesn't match.
                V != oldVal &&
                // And either:
                // We are not looking for a "real" old value.
                // The current value is a tombstone.
                // The current value is the initial value (VINITIAL).
                // (Essentially, any non-value match)
                (oldVal != MATCH_ANY || V == VTOMBSTONE || V == VINITIAL) &&
                // And either the value we wish to assign isn't VINITIAL or we aren't expecting a tombstone
                (V != VINITIAL || oldVal != VTOMBSTONE))
            {
                // Don't bother updating the table.
                return V;
            }

            // Atomically update the value.
            // NOTE: This can be an arbitrary function, such as increment.
            Value actualValue = CAS(table, idx, V, newVal);
            // If we succeeded.
            if (actualValue == V)
            {
                // Adjust the size counters for the table.
                // If this was a table copy, do nothing.
                if (oldVal != VINITIAL)
                {
                    // If we removed an initial or tombstone value with a non-tombstone.
                    if ((V == VINITIAL || V == VTOMBSTONE) && newVal != VTOMBSTONE)
                    {
                        table->chm.size.fetch_add(1);
                    }
                    // If we removed a non-initial and non-tombstone value with a tombstone.
                    else if (!(V == VINITIAL || V == VTOMBSTONE) && newVal == VTOMBSTONE)
                    {
                        table->chm.size.fetch_add(-1);
                    }
                }
                // If we replaced an initial value when we had expected a non-initial value, return the tombstone sentinel.
                return (V == VINITIAL && oldVal != VINITIAL) ? VTOMBSTONE : V;
            }
            // CAS failed.
            else
            {
                // Update the value with what we found during the failed CAS.
                V = actualValue;
            }
#ifdef RESIZE
            // If a primed value was is present (placed by us or someone else), re-run put on the new table.
            if (isMarked((uintptr_t)table->value(idx), MigrationFlag))
            {
                return putIfMatch(table->chm.copySlotAndCheck(this, table, idx, oldVal == VINITIAL), key, newVal, oldVal);
            }
#endif
            // Otherwise retry our put.
        }
    }
#ifdef RESIZE
    // Help to perform table migration, likely being assigned some range of values.
    // TODO: I have decided to assume the helper is always the top level table. This may not always be true.
    Table *helpCopy(Table *helper)
    {
        Table *topTable = this->table.load();

        // If there is no copy in progress, then there's nothing to be done here.
        if (topTable->chm.newTable.load() == nullptr)
        {
            return helper;
        }
        topTable->chm.helpCopyImpl(this, topTable, false);
        return helper;
    }
#endif
    void print()
    {
        Table *topTable = table.load();
        size_t len = topTable->len;
        for (size_t i = 0; i < len; i++)
        {

            Key key = topTable->key(i);
            Value value = topTable->value(i);
            // TODO: Try to make this pretty-printing of sentinels actually work.
            std::cout << "key: " << key << "value: " << value << "\n";
        }
        std::cout << std::endl;
    }

    static bool isKeyReserved(Key key)
    {
        return key == KINITIAL || key == KTOMBSTONE;
    }
    static bool isValueReserved(Value value)
    {
        return value == VINITIAL || value == VTOMBSTONE || value == TOMBPRIME || value == MATCH_ANY || value == NO_MATCH_OLD;
    }

private:
    // The structure that stores the top table.
    std::atomic<Table *> table;
};

// size_t keys and values.
// Initialization of sentinels.
// template <typename Key, typename Value>
// Value ConcurrentHashMap<Key, Value>::VINITIAL = new size_t();
// template <typename Key, typename Value>
// Value ConcurrentHashMap<Key, Value>::VTOMBSTONE = new size_t();
// template <typename Key, typename Value>
// Value ConcurrentHashMap<Key, Value>::TOMBPRIME = (size_t)setMark(VTOMBSTONE, MigrationFlag);
// template <typename Key, typename Value>
// Value ConcurrentHashMap<Key, Value>::MATCH_ANY = new size_t();
// template <typename Key, typename Value>
// Value ConcurrentHashMap<Key, Value>::NO_MATCH_OLD = new size_t();

// template <typename Key, typename Value>
// Key ConcurrentHashMap<Key, Value>::KINITIAL = new size_t();
// template <typename Key, typename Value>
// Key ConcurrentHashMap<Key, Value>::KTOMBSTONE = new size_t();

template <typename Key, typename Value, class Hash>
Value ConcurrentHashMap<Key, Value, Hash>::VINITIAL = ((((size_t)1 << 62) - 1) << 3);
template <typename Key, typename Value, class Hash>
Value ConcurrentHashMap<Key, Value, Hash>::VTOMBSTONE = ((((size_t)1 << 62) - 2) << 3);
template <typename Key, typename Value, class Hash>
Value ConcurrentHashMap<Key, Value, Hash>::TOMBPRIME = (size_t)setMark(VTOMBSTONE, MigrationFlag);
template <typename Key, typename Value, class Hash>
Value ConcurrentHashMap<Key, Value, Hash>::MATCH_ANY = ((((size_t)1 << 62) - 3) << 3);
template <typename Key, typename Value, class Hash>
Value ConcurrentHashMap<Key, Value, Hash>::NO_MATCH_OLD = ((((size_t)1 << 62) - 4) << 3);

template <typename Key, typename Value, class Hash>
Key ConcurrentHashMap<Key, Value, Hash>::KINITIAL = ((((size_t)1 << 62) - 1) << 3);
template <typename Key, typename Value, class Hash>
Key ConcurrentHashMap<Key, Value, Hash>::KTOMBSTONE = ((((size_t)1 << 62) - 2) << 3);

#endif
