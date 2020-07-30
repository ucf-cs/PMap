#ifndef PMwCAS_hpp
#define PMwCAS_hpp

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <chrono>
#include <utility>

#include "define.hpp"

#define SFENCE __builtin_ia32_sfence()
// TODO: Get this intrinsic, or some equivalent, working.
#define CLWB(p) //_mm_clwb(p)

// T: The data type being dealt with. Ideally, it should just be something word-sized.
// NOTE: T must not use the first 3 bits, and must not be larger than an atomic word.
// Ex. 61-bit int shifted left, pointers.
// K: The maximum number of words that can be atomically modified by the KCAS.
// P: The number of threads that can help perform operations.
template <class T, size_t K, size_t P>
class PMwCASManager
{
public:
    static const uintptr_t DirtyFlag = 1;
    static const uintptr_t PMwCASFlag = 2;
    static const uintptr_t RDCSSFlag = 4;
    // NOTE: These two flags are normally never set at the same time.
    // We get an extra bit out of this, so long as we help complete descriptors
    // before marking as migrated. If we didn't do this, then we may need to
    // migrate a descriptor, but we wouldn't be able to recognize it as a
    // descriptor since it was overwritten with the migration flag.
    // TODO: Make sure this flag is handled properly in all cases.
    static const uintptr_t MigrationFlag = PMwCASFlag | RDCSSFlag;
    // The mask includes all bits not used for flags.
    static const uintptr_t AddressMask = ~(DirtyFlag | PMwCASFlag | RDCSSFlag | MigrationFlag);

    // Forward declarations.
    struct Descriptor;
    struct Word;

    // Descriptor status.
    enum Status
    {
        // Avoid using the first 3 bits for our valid enum values.
        Undecided = 8,
        Succeeded = 16,
        Failed = 32,
    };
    // Descriptor reference.
    // Used for descriptor reuse.
    struct DescRef
    {
    private:
        static const size_t tidSize = (8 * sizeof(unsigned long) - __builtin_clzl(P) - 1) == 0 ? 1 : (8 * sizeof(unsigned long) - __builtin_clzl(P) - 1);
        static const size_t fieldIDSize = (8 * sizeof(unsigned long) - __builtin_clzl(K) - 1) == 0 ? 1 : (8 * sizeof(unsigned long) - __builtin_clzl(K) - 1);
        static const size_t isRDCSSSize = 1;
        static const size_t isPMwCASSize = 1;
        static const size_t isDirtySize = 1;
        // This will use whatever leftover bits remain while keeping the descriptor word-sized.
        static const size_t seqSize = 64 - tidSize - fieldIDSize - isRDCSSSize - isPMwCASSize - isDirtySize;

    public:
        // Thread ID.
        // Uses enough bits to address up to P threads.
        unsigned short tid : tidSize;
        // The word modified by the index.
        // Only used by RDCSS (word) descriptors.
        unsigned short fieldID : fieldIDSize;
        // A sequence number to avoid the ABA problem.
        unsigned long seq : seqSize;
        // Reserved bits.
        bool isRDCSS : isRDCSSSize;
        bool isPMwCAS : isPMwCASSize;
        bool isDirty : isDirtySize;

        // Base constructor.
        DescRef()
        {
            assert(sizeof(DescRef) == 8);
            tid = 0;
            fieldID = 0;
            seq = 0;
            isRDCSS = false;
            isPMwCAS = false;
            isDirty = false;
        }
        // Conversion contructor.
        // Defines conversion from T to DescRef.
        DescRef(T ptr)
        {
            assert(sizeof(T) == sizeof(DescRef));
            assert((tidSize + fieldIDSize + seqSize + isRDCSSSize + isPMwCASSize + isDirtySize) == 64);

            // TODO: Verify this works as expected.
            tid = ((size_t)ptr >> (fieldIDSize + seqSize + isRDCSSSize + isPMwCASSize + isDirtySize)) & (((size_t)1 << tidSize) - 1);
            fieldID = ((size_t)ptr >> (seqSize + isRDCSSSize + isPMwCASSize + isDirtySize)) & (((size_t)1 << fieldIDSize) - 1);
            seq = ((size_t)ptr >> (isRDCSSSize + isPMwCASSize + isDirtySize)) & (((size_t)1 << seqSize) - 1);
            isRDCSS = ((size_t)ptr >> (isPMwCASSize + isDirtySize)) & (((size_t)1 << isRDCSSSize) - 1);
            isPMwCAS = ((size_t)ptr >> isDirtySize) & (((size_t)1 << isPMwCASSize) - 1);
            isDirty = ((size_t)ptr >> 0) & (((size_t)1 << isDirtySize) - 1);
        }
        // Conversion function.
        // Defines conversion from DescRef to T.
        operator T() const
        {
            // TODO: Verify this works as expected.
            return (T)(((uintptr_t)tid << (fieldIDSize + seqSize + isRDCSSSize + isPMwCASSize + isDirtySize)) +
                       ((uintptr_t)fieldID << (seqSize + isRDCSSSize + isPMwCASSize + isDirtySize)) +
                       ((uintptr_t)seq << (isRDCSSSize + isPMwCASSize + isDirtySize)) +
                       ((uintptr_t)isRDCSS << (isPMwCASSize + isDirtySize)) +
                       ((uintptr_t)isPMwCAS << (isDirtySize)) +
                       ((uintptr_t)isDirty));
        }
    };
    // Word descriptor
    struct alignas(8) Word
    {
        std::atomic<T> *address;
        T oldVal;
        T newVal;
        bool operator<(const Word &word) const
        {
            return ((uintptr_t)address < (uintptr_t)word.address);
        }
    };
    // KCAS descriptor.
    struct alignas(8) Descriptor
    {
        // The status of the KCAS.
        std::atomic<Status> status;
        // The sequence number of the descriptor.
        // If this sequence number doesn't match, then it must be associated with a different KCAS.
        std::atomic<unsigned long> seq;
        // The number of words modified by the KCAS.
        size_t count;
        // The array of words modified by the KCAS.
        // NOTE: Everything beyond count elements are unused.
        alignas(8) Word words[K];
    };
    // Our descriptor pool.
    // NOTE: Word descriptors are built-in to KCAS descriptors.
    Descriptor descriptors[P];

    PMwCASManager()
    {
        // P and K should always be a power of 2.
        assert((P & (P - 1)) == 0);
        assert((K & (K - 1)) == 0);
        // Initialize the descriptors and words.
        for (size_t i = 0; i < P; i++)
        {
            // Though it doesn't particularly matter, we make sure all sequence numbers start at 0.
            descriptors[i].seq.store(0);
        }
        return;
    }

    DescRef createNew(size_t threadNum, size_t size, Word *words)
    {
        assert(threadNum < P);

        Descriptor *desc = &(descriptors[threadNum]);
        // Incrementing the sequence number will invalidate this descriptor.
        unsigned long oldSeq = desc->seq.load();
        // Update the sequnce number so our descriptor is considered new.
        desc->seq.store(oldSeq + 1);
        // Sort the words.
        std::sort(words, words + size);
        // Make sure the words are sorted correctly.
        for (size_t i = 0; i < size; i++)
        {
            std::cout << words[i].address << std::endl;
        }
        for (size_t i = 0; i < size - 1; i++)
        {
            assert(((uintptr_t)words[i].address) < ((uintptr_t)words[i + 1].address));
        }
        // Construct the new descriptor.
        desc->status = Undecided;
        desc->count = size;
        for (size_t i = 0; i < size; i++)
        {
            // TODO: Ensure all values copy over properly.
            desc->words[i] = words[i];
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
        }

        // Update the sequence number again.
        desc->seq.store(oldSeq + 2);
        // Return a descriptor reference.
        DescRef ref;
        ref.tid = threadNum;
        ref.seq = oldSeq + 2;
        // We make no assumptions here. These flags must be set later.
        ref.isRDCSS = false;
        ref.isPMwCAS = false;
        ref.isDirty = false;
        return ref;
    }
    // These functions must support address (std::atomic<T> *) and status (std::atomic<Status> *)
    template <class U>
    U readField(DescRef desc, std::atomic<U> *field, bool &success)
    {
        U result = field->load();
        if (desc.seq != descriptors[desc.tid].seq)
        {
            success = false;
            // NOTE: We shouldn't actually read this, but we need to return something.
            // Always check success to validate first.
            return result;
        }
        return result;
    }
    template <class U>
    bool writeField(DescRef desc, U value, std::atomic<U> *field, bool &success)
    {
        while (true)
        {
            U expVal = field->load();
            if (desc.seq != descriptors[desc.tid].seq)
            {
                success = false;
                return false;
            }
            U newVal = value;
            if (CASField<U>(desc, expVal, newVal, field, success))
            {
                return true;
            }
        }
    }
    template <class U>
    U CASField(DescRef desc, U &fExp, U fNew, std::atomic<U> *field, bool &success)
    {
        while (true)
        {
            U expVal = field->load();
            if (desc.seq != descriptors[desc.tid].seq)
            {
                success = false;
                return expVal;
            }
            if (expVal != fExp)
            {
                return fExp;
            }
            if (field->compare_exchange_strong(fExp, fNew))
            {
                return fNew;
            }
        }
    }

    // Fast sort for small arrays.
    // TODO: Unused. Consider removing.
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

    // Used to hide descriptor construction.
    // Format: address, oldVal, newVal
    bool PMwCAS(size_t threadNum, size_t size, Word *words)
    {
        // Create a "new" descriptor.
        DescRef desc = createNew(threadNum, size, words);

        // Run PMwCAS on our generated descriptor.
        return PMwCAS(desc);
    }
    // Perform a persistent, multi-word CAS based on a descriptor pointer.
    bool PMwCAS(DescRef desc)
    {
        // The status we will assign to our KCAS. Defaults to success unless changed.
        Status st = Succeeded;
        // Must operate in a fixed address traversal order.
        // NOTE: We will assume the appropriate order was used when the descriptor was made.
        for (size_t i = 0; i < descriptors[desc.tid].count; i++)
        {
        retry:
            // Create a word descriptor
            DescRef wordDesc = desc;
            wordDesc.fieldID = i;
            wordDesc.isRDCSS = true;
            wordDesc.isPMwCAS = false;
            wordDesc.isDirty = false;

            // Attempt to place the descriptor using RDCSS.
            T rval = InstallMwCASDescriptor(wordDesc);
            // If the installation succeeded.
            if (rval == descriptors[desc.tid].words[i].oldVal)
            {
                // Continue to the next word.
                continue;
            }
            // If it failed because of another PMwCAS in progress.
            else if (((DescRef)rval).isPMwCAS)
            {
                // If the value stored there has not yet been persisted.
                if (((DescRef)rval).isDirty)
                {
                    // Persist it.
                    persist(descriptors[desc.tid].words[i].address, rval);
                }
                // We clashed with a PMwCAS. Help complete it.
                PMwCAS((DescRef)rval);
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
            for (size_t i = 0; i < descriptors[desc.tid].count; i++)
            {
                // Recreate the expected word descriptor
                DescRef persistDesc = desc;
                persistDesc.fieldID = i;
                persistDesc.isRDCSS = false;
                persistDesc.isPMwCAS = true;
                persistDesc.isDirty = true;
                persist(descriptors[desc.tid].words[i].address, (T)persistDesc);
                // DEBUG: This should typically succeed. Keep a close eye on it.
                if (((DescRef)descriptors[desc.tid].words[i].address->load()).isDirty)
                {
                    std::cout << "Failed to persist!" << std::endl;
                }
            }
        }

        // Finalize the status of the PMwCAS, whether success or failure.
        Status expectedStatus = Undecided;
        bool success = true;
        CASField<Status>(desc, expectedStatus, (Status)((uintptr_t)st | DirtyFlag), &descriptors[desc.tid].status, success);
        Status status = readField<Status>(desc, &descriptors[desc.tid].status, success);
        // If the descriptor changed from under us, then the PMwCAS is already over.
        if (!success)
        {
            return false;
        }
        if ((uintptr_t)status & DirtyFlag)
        {
            // TODO: Verify success. What does failure mean here?
            PMwCASManager<Status, K, P>::persist(&descriptors[desc.tid].status, status);
            status = (Status)((uintptr_t)status & ~DirtyFlag);
        }

        // Install the final values.
        for (size_t i = 0; i < descriptors[desc.tid].count; i++)
        {
            // If we succeeded, we will place the new values.
            // If we failed, we will restore the old values.
            T v = ((status == Succeeded)
                       ? descriptors[desc.tid].words[i].newVal
                       : descriptors[desc.tid].words[i].oldVal);
            // We expect a PMwCAS descriptor that hasn't persisted.
            T expected = (T)((uintptr_t)desc | PMwCASFlag | DirtyFlag);
            // Replace it with a value, unpersisted.
            T rval = (T)expected;
            CASField<T>(desc, rval, (T)((uintptr_t)v | DirtyFlag), descriptors[desc.tid].words[i].address, success);
            if (!success)
            {
                break;
            }

            // If we failed because the descriptor was persisted.
            if ((uintptr_t)rval == ((uintptr_t)desc | PMwCASFlag))
            {
                // Try again, assuming that the descriptor *is* persisted.
                T expected2 = (T)((uintptr_t)expected & ~DirtyFlag);
                CASField<T>(desc, expected2, v, descriptors[desc.tid].words[i].address, success);
                if (!success)
                {
                    break;
                }
            }
            // Persist any change that occurs.
            persist(descriptors[desc.tid].words[i].address, v);
        }
        // Return our final success (or failure).
        assert((status & AddressMask) != Undecided);
        return (status == Succeeded);
    }
    // Attempt to read an address.
    // We must ensure all flag conditions have been handled before reading the address.
    T PMwCASRead(std::atomic<T> *address)
    {
        while (true)
        {
            // Read the value.
            T v = address->load();
            // Make sure the RDCSSFlag is at the same bit as the isRDCSS bitfield.
            assert(((bool)(((uintptr_t)v & RDCSSFlag) != 0)) == ((bool)(((DescRef)v).isRDCSS)));
            // If it's part of an RDCSS.
            if ((uintptr_t)v & RDCSSFlag)
            {
                // Finish the RDCSS.
                CompleteInstall((DescRef)v);
                // And try to read it again.
                continue;
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
                PMwCAS((DescRef)v);
                // And try to read it again.
                continue;
            }
            // Return the final value read.
            return v;
        }
    }
    // Flush and fence the data stored in the address, then unflag the dirty bit atomically.
    static T persist(std::atomic<T> *address, T value)
    {
        // TEMP: Ensure the expected value actually matches most of the time.
        CLWB(address);
        SFENCE;
        address->compare_exchange_strong(value, (T)((uintptr_t)value & ~DirtyFlag));
        return value;
    }

private:
    // Use RDCSS to replace a value with a descriptor.
    // desc should reference a valid tid and fieldID to identify the correct word descriptor.
    T InstallMwCASDescriptor(DescRef desc)
    {
        // Mark our descriptor.
        desc.isRDCSS = true;
        T val;
        while (true)
        {
            // Flag to indicate whether or not our descriptor modifications worked.
            bool success = true;
            // This old value could be associated with a newer descriptor.
            // CASField will fail if the sequence numbers don't match, so problems with using a newer descriptor by accident will be handled.
            T oldVal = descriptors[desc.tid].words[desc.fieldID].oldVal;
            // This will be replaced with whatever was actually in the address field when we did the CAS.
            val = oldVal;
            // Attempt the replacement.
            CASField<T>(desc, val, (T)desc, descriptors[desc.tid].words[desc.fieldID].address, success);
            // CAS can fail if the sequence numbers didn't match.
            if (!success)
            {
                // TODO: Need to report sequence mismatch failure here, somehow.
                return val;
            }
            // Make sure the RDCSSFlag is at the same bit as the isRDCSS bitfield.
            assert(((bool)(((uintptr_t)val & RDCSSFlag) != 0)) == ((bool)(((DescRef)val).isRDCSS)));
            // If the value was an RDCSS descriptor.
            if (((DescRef)val).isRDCSS)
            {
                // Help finish the RDCSS.
                CompleteInstall((DescRef)val);
                // Try the installation again.
                continue;
            }
            // If the value matched what we had expected.
            else if (val == oldVal)
            {
                // Finish the installation.
                CompleteInstall(desc);
            }
            break;
        }
        // return the value previously in the target address.
        return val;
    }
    // Complete the RDCSS operation.
    void CompleteInstall(DescRef desc)
    {
        // Prepare to place the new value (a KCAS descriptor), marked initially as dirty and part of PMwCAS.
        DescRef ptr = desc;
        assert(ptr.tid == desc.tid);
        assert(ptr.seq == desc.seq);
        ptr.isRDCSS = false;
        ptr.isPMwCAS = true;
        ptr.isDirty = true;

        bool success = true;
        // Determine whether we are placing the KCAS descriptor or restoring the old value.
        bool u = (readField<Status>(desc, &descriptors[desc.tid].status, success) == Undecided);
        // Mismatch in sequence numbers just means some other thread finished this already.
        if (!success)
        {
            return;
        }
        // Attempt the CAS. If we fail, it just means some other thread succeeded.
        T expected = (T)desc;
        // This should be always set already. Check to make sure.
        assert(((uintptr_t)expected & RDCSSFlag) != 0);
        expected = (uintptr_t)expected | RDCSSFlag;
        T oldVal = descriptors[desc.tid].words[desc.fieldID].oldVal;
        CASField<T>(desc, expected, u ? (T)ptr : oldVal, descriptors[desc.tid].words[desc.fieldID].address, success);
        return;
    }
};

#endif