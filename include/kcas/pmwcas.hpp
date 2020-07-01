#ifndef PMwCAS_hpp
#define PMwCAS_hpp

#include <assert.h>

#include <atomic>
#include <cstddef>
#include <chrono>
#include <utility>

#include "define.hpp"

#define SFENCE void __builtin_ia32_sfence(void)

// NOTE: T must not use the first 4 bits, and must not be larger than an atomic word.
// Ex. 60-bit int shifted left.
// Ex. pointer?
template <class T, size_t K>
class PMwCASManager
{
    // TODO: Making everything public is a hack.
public:
    static const uintptr_t DirtyFlag = 1;
    static const uintptr_t PMwCASFlag = 2;
    static const uintptr_t RDCSSFlag = 4;
    //static const uintptr_t MigrationFlag = 8;
    //static const uintptr_t AddressMask = ~15;
    static const uintptr_t AddressMask = ~7;

    // Forward declarations.
    struct Descriptor;

    enum Status
    {
        // Avoid using the first 4 bits for our valid enum values.
        Undecided = 16,
        Succeeded = 32,
        Failed = 64,
    };
    // Word descriptor
    struct alignas(64) Word
    {
        std::atomic<T> *address;
        T oldVal;
        T newVal;
        Descriptor *mwcasDescriptor;
    };
    // KCAS descriptor.
    struct alignas(64) Descriptor
    {
        std::atomic<Status> status;
        size_t count;
        alignas(64) Word words[K];
    };
    // Perform a persistent, multi-word CAS based on a descriptor pointer.
    bool PMwCAS(Descriptor *md)
    {
        //assert(((uintptr_t)md & ~AddressMask) == 0);
        // The status of our KCAS. Defaults to success unless changed.
        Status st = Succeeded;
        // TODO: Must operates in fixed address traversal order.
        // For now, we will assume the appropriate order was used when the descriptor was made.
        for (size_t i = 0; i < md->count; i++)
        {
        retry:
            // Create a new word descriptor, so we can make sure the reference doesn't change from under it.
            Word *wd = new Word();
            assert((uintptr_t)wd % 64 == 0);
            wd->address = md->words[i].address;
            wd->oldVal = md->words[i].oldVal;
            wd->newVal = md->words[i].newVal;
            wd->mwcasDescriptor = md->words[i].mwcasDescriptor;
            assert(wd->mwcasDescriptor != NULL);
            assert(wd->address != NULL);
            // TODO: Until descriptor reuse is implemented, this is a memory leak.

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
        bool successDebug = md->status.compare_exchange_strong(expectedStatus, (Status)((uintptr_t)st | DirtyFlag));
        Status status = md->status.load();
        if ((uintptr_t)status & DirtyFlag)
        {
            PMwCASManager<Status, K>::persist(&(md->status), status);
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
        // Prepare to place the new value (a KCAS descriptor), marked initially as dirty and part of PMwCAS.
        T ptr = (T)((uintptr_t)wd->mwcasDescriptor | PMwCASFlag | DirtyFlag);
        // Determine whether we are placing the KCAS descriptor or restoring the old value.
        bool u = (wd->mwcasDescriptor->status.load() == Undecided);
        // Attempt the CAS. If we fail, it just means some other thread succeeded.
        T expected = (T)((uintptr_t)wd | RDCSSFlag);
        wd->address->compare_exchange_strong(expected, u ? ptr : wd->oldVal);
        return;
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
        // TODO: Implement CLWB
        //CLWB(address);
        SFENCE;
        address->compare_exchange_strong(value, (T)((uintptr_t)value & ~DirtyFlag));
        return value;
    }
};

#endif