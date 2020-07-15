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

std::hash<void *> hasher;

template <class Key, class Value>
class ConcurrentHashMap
{
    // Hash function.
    static size_t hash(Key key)
    {
        // xxhash hashing.
        std::array<Key, 1> input{key};
        xxh::hash_t<64> hash = xxh::xxhash<64>(input);

        // C++ standard library hashing.
        //size_t hash = hasher(key);

        // hash=key naive hashing.
        //size_t hash = (size_t)key;

        // Debug to determine what hashes we are generating.
        //std::cout << hash << std::endl;

        return ((size_t)hash);
    }

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
    // A single table.
    // Multiple tables can exist at a time during resizing.
    class Table
    {
    public:
        // The hash map control structure.
        class CHM
        {
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

        public:
            // The number of active KV pairs.
            // If this number gets too large, consider resizing.
            std::atomic<size_t> size;

            // The number of usable slots.
            // If this number gets too large, consider resizing.
            std::atomic<size_t> slots;

            CHM(size_t tableCapacity, size_t existingSize)
            {
                this->size.store(existingSize);
                slots.store(tableCapacity);
            }
            // NOTE: This constructor should not be used. Instead, any initialized CHM should provide a table capacity and existing size before use.
            CHM()
            {
                //CHM(Table::MIN_SIZE);
                this->size.store(0);
                slots.store(Table::MIN_SIZE);
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
        };
        typedef struct KVpair
        {
            std::atomic<Key> key;
            std::atomic<Value> value;
        } KVpair;
        // NOTE: Hashes are only needed if pointer comparison is insuccicient for comparison, so we don't use it in this implementation.
        // Keys and values.
        KVpair *pairs;

        // Minimum table size.
        // Must always be a power of two.
        const static size_t MIN_SIZE = 1 << 3;

        // CHM: Hash Table Control Structure.
        CHM chm;
        // The number of pairs that can fit in the table.
        size_t len;

        Table(size_t tableCapacity, size_t existingSize)
        {
            assert(tableCapacity % 2 == 0);
            assert(tableCapacity >= MIN_SIZE);
            new (&chm) CHM(tableCapacity, existingSize);
            pairs = new KVpair[tableCapacity];
            for (size_t i = 0; i < tableCapacity; i++)
            {
                // Initialize these to a default, reserved value.
                pairs[i].key.store(KINITIAL);
                pairs[i].value.store(VINITIAL);
            }
            len = tableCapacity;
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
        static Value CASvalue(Table *table, size_t idx, Value oldValue, Value newValue)
        {
            assert(idx < table->len);
            Key oldValueRef = oldValue;
            table->pairs[idx].value.compare_exchange_strong(oldValueRef, newValue);
            return oldValueRef;
        }

        // Function to increment a value.
        static Value increment(Table *table, size_t idx, Value oldValue, Value newValue)
        {
            assert(idx < table->len);
            Key oldValueRef = oldValue;
            if (oldValue == VTOMBSTONE)
            {
                oldValue = 0;
            }
            // Our actual new value here is dependent on the old value.
            // NOTE: The correct way to perform this addition is entirely dependent on the type of Value.
            newValue = ((oldValue >> 3) + (newValue >> 3)) << 3;
            // Must be CAS rather than FAA because the old value might be a sentinel.
            table->pairs[idx].value.compare_exchange_strong(oldValueRef, newValue);
            return oldValueRef;
        }
    };

    // Constructors.
    ConcurrentHashMap()
    {
        //ConcurrentHashMap(Table::MIN_SIZE);
        Table *newTable = new Table(Table::MIN_SIZE, 0);
        table.store(newTable);
        return;
    }
    ConcurrentHashMap(size_t size)
    {
        Table *newTable = new Table(size, 0);
        table.store(newTable);
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
        assert(!IS_MARKED(retVal, 0));
        // TODO: Can we safely ignore this?
        //assert(retVal != VINITIAL);
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

            // Compare the key we found.
            // We do this because multiple keys can hash to the same index.
            if (keyEq(K, key))
            {
                // We found the target key.
                return (V == VTOMBSTONE) ? VINITIAL : V;
            }

            // If we have exceeded our reprobe limit.
            if (++reprobeCount >= reprobeLimit(len) ||
                // Or if we found a tombstone key, indicating there are no more keys in this table.
                K == KTOMBSTONE)
            {
                // Value is not present.
                return VINITIAL;
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
    Value putIfMatch(Table *table, Key key, Value newVal, Value oldVal,
                     Value CAS(Table *table, size_t idx, Value oldValue, Value newValue) = &Table::CASvalue)
    {
        assert(newVal != VINITIAL);
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
            K = table->key(idx);
            V = table->value(idx);

            // If the slot is free.
            if (K == VINITIAL)
            {
                // If we find an empty slot, the key was never in the table.

                // If we were trying to remove the key.
                if (newVal == ConcurrentHashMap<Key, Value>::VTOMBSTONE)
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
                // The key is not present.
                return VINITIAL;
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
            // Otherwise retry our put.
        }
    }

    void print()
    {
        Table *topTable = table.load();
        size_t len = topTable->len;
        for (size_t i = 0; i < len; i++)
        {

            Key key = topTable->key(i);
            Value value = topTable->value(i);
            // TODO: Try to make this pretty-printing of sentinels actually work.
            //std::cout << ((Key)key == MATCH_ANY ? "MATCH_ANY" : ((Key)key == NO_MATCH_OLD ? "NO_MATCH_OLD" : ((Key)key == KTOMBSTONE ? "TOMBSTONE" : key))) << "\t" << ((Value)value == MATCH_ANY ? "MATCH_ANY" : ((Value)value == NO_MATCH_OLD ? "NO_MATCH_OLD" : ((Value)value == VTOMBSTONE ? "TOMBSTONE" : ((Value)value == TOMBPRIME ? "TOMBPRIME" : value)))) << "\n";
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

// Initialization of sentinels.
template <typename Key, typename Value>
Value ConcurrentHashMap<Key, Value>::VINITIAL = ((((size_t)1 << 62) - 1) << 3);
template <typename Key, typename Value>
Value ConcurrentHashMap<Key, Value>::VTOMBSTONE = ((((size_t)1 << 62) - 2) << 3);
template <typename Key, typename Value>
Value ConcurrentHashMap<Key, Value>::TOMBPRIME = (size_t)SET_MARK(VTOMBSTONE, 0);
template <typename Key, typename Value>
Value ConcurrentHashMap<Key, Value>::MATCH_ANY = ((((size_t)1 << 62) - 3) << 3);
template <typename Key, typename Value>
Value ConcurrentHashMap<Key, Value>::NO_MATCH_OLD = ((((size_t)1 << 62) - 4) << 3);

template <typename Key, typename Value>
Key ConcurrentHashMap<Key, Value>::KINITIAL = ((((size_t)1 << 62) - 1) << 3);
template <typename Key, typename Value>
Key ConcurrentHashMap<Key, Value>::KTOMBSTONE = ((((size_t)1 << 62) - 2) << 3);

#endif