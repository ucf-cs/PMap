#ifndef PMwCAS_hpp
#define PMwCAS_hpp

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <chrono>
#include <utility>

#include "define.hpp"

#define SFENCE void __builtin_ia32_sfence(void)
#define CLWB void _mm_clwb(void const *p);

// TODO: OVERALL: Add support for descriptor reuse. This involves creating per-thread descriptors (KCAS and RDCSS descriptors) and accessing descriptor fields using custom read and CAS logic implemented in the Trevor Brown paper on descriptor reuse. One that work is done, the PMwCAS will be complete.

// NOTE: T must not use the first 4 bits, and must not be larger than an atomic word.
// Ex. 60-bit int shifted left.
// Ex. pointer?
template <class T, size_t K, size_t P>
class PMwCASManager
{
public:
    static const uintptr_t DirtyFlag = 1;
    static const uintptr_t PMwCASFlag = 2;
    static const uintptr_t RDCSSFlag = 4;
    //static const uintptr_t MigrationFlag = 8;
    //static const uintptr_t AddressMask = ~15;
    static const uintptr_t AddressMask = ~7;

    // Forward declarations.
    struct Descriptor;
    struct Word;

    enum Status
    {
        // Avoid using the first 3 bits (4 in case I find a way to need all 4) for our valid enum values.
        Undecided = 16,
        Succeeded = 32,
        Failed = 64,
    };
    // Descriptor reference.
    // Used for descriptor reuse.
    // TODO: Do some testing to confirm this is 64 bits and that bits are placed as expected.
    struct DescRef
    {
        // Thread ID.
        // can only address 2^width-1 threads.
        unsigned int tid : 8;
        // A sequence number to avoid the ABA problem.
        unsigned int seq : 53;
        // Reserved bits.
        bool isRDCSS : 1;
        bool isPMwCAS : 1;
        bool isDirty : 1;
    };
    // Word descriptor
    struct alignas(64) Word
    {
        // TODO: Support multiple word types?
        std::atomic<T> *address;
        T oldVal;
        T newVal;
        Descriptor *mwcasDescriptor;
        bool operator<(const Word &word) const
        {
            return ((uintptr_t)address < (uintptr_t)word.address);
        }
    };
    // KCAS descriptor.
    struct alignas(64) Descriptor
    {
        std::atomic<Status> status;
        size_t count;
        alignas(64) Word words[K];
    };
    // Our descriptor pools.
    Descriptor descriptors[P];
    Word words[P * K];

    // Fast sort for small arrays.
    void insertionSortRecursive(size_t n, size_t *addresses, size_t *sorts)
    {
        if (n <= 0)
        {
            return;
        }
        insertionSortRecursive(n - 1, addresses, sorts);
        size_t x = addresses[n];
        size_t y = sorts[n];
        size_t j = n - 1;
        while (j >= 0 && addresses[j] > x)
        {
            addresses[j + 1] = addresses[j];
            sorts[j + 1] = sorts[j];
        }
        addresses[j + 1] = x;
        sorts[j + 1] = y;
        return;
    }

    // Perform a persistent, multi-word CAS based on a descriptor pointer.
    bool PMwCAS(Descriptor *md)
    {
        //assert(((uintptr_t)md & ~AddressMask) == 0);
        // The status of our KCAS. Defaults to success unless changed.
        Status st = Succeeded;
        // Must operate in a fixed address traversal order.
        // We will assume the appropriate order was used when the descriptor was made.
        for (size_t i = 0; i < md->count; i++)
        {
        retry:
            // Create a new word descriptor, so we can make sure the reference doesn't change from under it.
            // TODO: Reference the word descriptor that already exists in the MwDescriptor instead.
            Word *wd = new Word();
            assert((uintptr_t)wd % 64 == 0);
            wd->address = md->words[i].address;
            wd->oldVal = md->words[i].oldVal;
            wd->newVal = md->words[i].newVal;
            wd->mwcasDescriptor = md->words[i].mwcasDescriptor;
            assert(wd->mwcasDescriptor != NULL);
            assert(wd->address != NULL);

            // Attempt to place the descriptor using RDCSS.
            T rval = InstallMwCASDescriptor(wd, md);
            // If the installation succeeded.
            if (rval == md->words[i].oldVal)
            {
                // Continue to the next word.
                continue;
            }
            // If it failed because of another PMwCAS in progress.
            else if (((uintptr_t)rval & PMwCASFlag) != 0)
            {
                // If the value stored there has not yet been persisted.
                if (((uintptr_t)rval & DirtyFlag) != 0)
                {
                    // Persist it.
                    persist(md->words[i].address, rval);
                }
                // We clashed with a PMwCAS. Help complete it.
                PMwCAS((Descriptor *)rval);
                // Now that the obstruction is removed, try again.
                goto retry;
            }
            // If it failed because the expected value didn't match.
            else
            {
                // Our PMwCAS failed.
                st = Failed;
                // Don't bother working on other words.
                // We have failed.
                break;
            }
        }

        // If we successfully inserted descriptors at all locations.
        if (st == Succeeded)
        {
            // Persist all target words.
            for (size_t i = 0; i < md->count; i++)
            {
                persist(md->words[i].address, (T)((uintptr_t)md | PMwCASFlag | DirtyFlag));
            }
        }

        // Finalize the status of the PMwCAS, whether success or failure.
        Status expectedStatus = Undecided;
        md->status.compare_exchange_strong(expectedStatus, (Status)((uintptr_t)st | DirtyFlag));
        Status status = md->status.load();
        if ((uintptr_t)status & DirtyFlag)
        {
            PMwCASManager<Status, K, P>::persist(&(md->status), status);
        }

        // Install the final values.
        for (size_t i = 0; i < md->count; i++)
        {
            // If we succeeded, we will place the new values.
            // If we failed, we will restore the old values.
            T v = ((md->status.load() == Succeeded) ? md->words[i].newVal : md->words[i].oldVal);
            // We expect a PMwCAS descriptor that hasn't persisted.
            T expected = (T)((uintptr_t)md | PMwCASFlag | DirtyFlag);
            // Replace it with a value, unpersisted.
            T rval = expected;
            md->words[i].address->compare_exchange_strong(rval, (T)((uintptr_t)v | DirtyFlag));
            // If we failed because the descriptor was persisted.
            if (rval == ((T)((uintptr_t)md | PMwCASFlag)))
            {
                // Try again, assuming that the descriptor *is* persisted.
                T expected2 = (T)((uintptr_t)expected & ~DirtyFlag);
                md->words[i].address->compare_exchange_strong(expected2, v);
                // Don't bother checking if this succeeded. If both of these CAS operations failed, it means some other thread succeeded.
            }
            // Persist any change that occurs.
            persist(md->words[i].address, v);
        }
        // Return our final success (or failure).
        return (md->status.load() == Succeeded);
    }
    // Used to hide descriptor construction.
    // Format: address, oldVal, newVal
    bool PMwCAS(size_t threadNum, size_t size, Word *words)
    {
        // Sort the words.
        std::sort(words, words + size);
        // TODO: Make sure they are sorted correctly.

        // Get the current thread's descriptor.
        // TODO: Switch this to descriptor reuse.
        Descriptor *desc = new Descriptor(); // descriptors[threadNum];

        // Construct the descriptor.
        desc->status = Undecided;
        desc->count = size;
        for (size_t i = 0; i < size; i++)
        {
            // TODO: Ensure all values copy over properly.
            desc->words[i] = words[i];
            desc->words[i].mwcasDescriptor = desc;
        }

        // Validate the descriptor.
        assert((desc->status.load() == Undecided));
        assert(desc->count <= K);
        for (size_t j = 0; j < desc->count; j++)
        {
            assert(desc->words[j].address != NULL);
            assert((uintptr_t)desc->words[j].address > 100);
            assert((uintptr_t)desc->words[j].address >= ((uintptr_t)&array[0]));
            assert((uintptr_t)desc->words[j].address <= ((uintptr_t)&array[0] + (8 * ARRAY_SIZE)));
            assert(desc->words[j].mwcasDescriptor == desc);
        }

        // Run PMwCAS on our generated descriptor.
        return PMwCAS(desc);
    }
    // Attempt to read an address.
    // We must ensure all flag conditions have been handled before reading the address.
    T PMwCASRead(std::atomic<T> *address)
    {
    retry:
        // Read the value.
        T v = address->load();
        // If it's part of an RDCSS.
        if ((uintptr_t)v & RDCSSFlag)
        {
            // Finish the RDCSS.
            CompleteInstall((Word *)((uintptr_t)v & AddressMask));
            // And try to read it again.
            goto retry;
        }
        // If the value has not been persisted.
        if ((uintptr_t)v & DirtyFlag)
        {
            // Persist it.
            persist(address, v);
            // And remove that mark.
            v = (uintptr_t)v & ~DirtyFlag;
        }
        // If the value is part of a PMwCAS.
        if ((uintptr_t)v & PMwCASFlag)
        {
            // Help complete the KCAS.
            PMwCAS((Descriptor *)((uintptr_t)v & AddressMask));
            // And try to read it again.
            goto retry;
        }
        // Return the final value read.
        return v;
    }
    // Flush and fence the data stored in the address, then unflag the dirty bit atomically.
    static T persist(std::atomic<T> *address, T value)
    {
        CLWB(address);
        SFENCE;
        address->compare_exchange_strong(value, (T)((uintptr_t)value & ~DirtyFlag));
        return value;
    }

private:
    // Use RDCSS to replace a value with a descriptor.
    // Pass the word descriptor by reference.
    T InstallMwCASDescriptor(Word *wd, Descriptor *md)
    {
        assert(wd->mwcasDescriptor == md);
        // Mark our descriptor.
        T ptr = (T)((uintptr_t)wd | RDCSSFlag);
    retry:
        // Attempt the replacement.
        T val = wd->oldVal;
        wd->address->compare_exchange_strong(val, ptr);
        // If the value was an RDCSS descriptor.
        if (((uintptr_t)val & RDCSSFlag) != 0)
        {
            // Help finish the RDCSS.
            CompleteInstall((Word *)((uintptr_t)val & AddressMask));
            // Try the installation again.
            goto retry;
        }
        // If the value matched what we had expected.
        if (val == wd->oldVal)
        {
            // Finish the installation.
            CompleteInstall(wd);
        }
        // return the value previously in the target address.
        return val;
    }
    // Complete the RDCSS operation.
    void CompleteInstall(Word *wd)
    {
        // Ensure the placement is valid.
        bool valid = false;
        for (size_t i = 0; i < wd->mwcasDescriptor->count; i++)
        {
            if (wd->mwcasDescriptor->words[i].address == wd->address)
            {
                valid = true;
                break;
            }
        }
        assert(valid);

        // Prepare to place the new value (a KCAS descriptor), marked initially as dirty and part of PMwCAS.
        T ptr = (T)((uintptr_t)wd->mwcasDescriptor | PMwCASFlag | DirtyFlag);
        // Determine whether we are placing the KCAS descriptor or restoring the old value.
        bool u = (wd->mwcasDescriptor->status.load() == Undecided);
        // Attempt the CAS. If we fail, it just means some other thread succeeded.
        T expected = (T)((uintptr_t)wd | RDCSSFlag);
        wd->address->compare_exchange_strong(expected, u ? ptr : wd->oldVal);
        return;
    }
};

#endif