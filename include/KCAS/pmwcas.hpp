#ifndef PMwCAS_hpp
#define PMwCAS_hpp

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <chrono>
#include <utility>

#include "define.hpp"
#include "../persistence.hpp"
#include "../marking.hpp"

thread_local size_t localThreadNum;
thread_local size_t helps = 0;
thread_local size_t opsDone = 0;

// T: The data type being dealt with. Ideally, it should just be something word-sized.
// NOTE: T must not use the first 3 bits, and must not be larger than an atomic word.
// NOTE: T must be able to cast to and from a 64-bit uintptr_t.
// Ex. 61-bit int shifted left, pointers.
// K: The maximum number of words that can be atomically modified by the KCAS.
// P: The number of threads that can help perform operations.
template <class T, size_t K, size_t P>
class PMwCASManager
{
public:
    // Forward declarations.
    struct Word;
    struct WordDescriptor;
    struct KCASDescriptor;

    // Descriptor status.
    enum Status
    {
        // NOTE: When using this atomically, avoid the first 3 bits, as they are reserved.
        // This can be (and is) handled properly using bitfield logic.
        Undecided = 0,
        Succeeded = 1,
        Failed = 2,
    };
    // Descriptor reference.
    // Used for descriptor reuse.
    struct DescRef
    {
    private:
        static const size_t tidSize = (8 * sizeof(unsigned long) - __builtin_clzl(P) - 1) == 0 ? 1 : (8 * sizeof(unsigned long) - __builtin_clzl(P) - 1);
        static const size_t isRDCSSSize = 1;
        static const size_t isKCASSize = 1;
        static const size_t isDirtySize = 1;
        // This will use whatever leftover bits remain while keeping the descriptor word-sized.
        static const size_t seqSize = 64 - tidSize - isRDCSSSize - isKCASSize - isDirtySize;

    public:
        // Reserved bits.
        bool isDirty : isDirtySize;
        bool isKCAS : isKCASSize;
        bool isRDCSS : isRDCSSSize;
        // A sequence number to avoid the ABA problem.
        unsigned long seq : seqSize;
        // Thread ID.
        // Uses enough bits to address up to P threads.
        unsigned short tid : tidSize;

        // Base constructor.
        DescRef() noexcept : isDirty(false), isKCAS(false), isRDCSS(false), seq(0), tid(0)
        {
            assert(sizeof(DescRef) == 8);
        }
        // Conversion contructor.
        // Defines conversion from T to DescRef.
        // That way, we can read the fields contained within.
        DescRef(T ptr)
        {
            assert(sizeof(T) == sizeof(DescRef));
            assert((tidSize + seqSize + isRDCSSSize + isKCASSize + isDirtySize) == 64);

            tid = ((size_t)ptr >> (seqSize + isRDCSSSize + isKCASSize + isDirtySize)) & (((size_t)1 << tidSize) - 1);
            seq = ((size_t)ptr >> (isRDCSSSize + isKCASSize + isDirtySize)) & (((size_t)1 << seqSize) - 1);
            isRDCSS = ((size_t)ptr >> (isKCASSize + isDirtySize)) & (((size_t)1 << isRDCSSSize) - 1);
            isKCAS = ((size_t)ptr >> isDirtySize) & (((size_t)1 << isKCASSize) - 1);
            isDirty = ((size_t)ptr >> 0) & (((size_t)1 << isDirtySize) - 1);
        }
        // Conversion function.
        // Defines conversion from DescRef to T.
        // Used for placing a DescRef directly in the data structure.
        operator T() const
        {
            assert(sizeof(T) == sizeof(DescRef));
            return (T)(((uintptr_t)tid << (seqSize + isRDCSSSize + isKCASSize + isDirtySize)) +
                       ((uintptr_t)seq << (isRDCSSSize + isKCASSize + isDirtySize)) +
                       ((uintptr_t)isRDCSS << (isKCASSize + isDirtySize)) +
                       ((uintptr_t)isKCAS << (isDirtySize)) +
                       ((uintptr_t)isDirty));
        }

        // Test the behavior of casting to make sure nothing is lost or misplaced.
        static void testCast()
        {
            DescRef desc;
            T castDesc;
            DescRef castBackDesc;
            for (size_t i = 0; i < tidSize; i++)
            {
                for (size_t j = 0; j < seqSize; j++)
                {
                    desc.tid = i;
                    desc.seq = j;
                    castDesc = (T)desc;
                    for (size_t l = 0; l < isRDCSSSize; l++)
                    {
                        for (size_t m = 0; m < isKCASSize; m++)
                        {
                            for (size_t n = 0; n < isDirtySize; n++)
                            {
                                if (l != 0)
                                {
                                    desc.isRDCSS = true;
                                    castDesc = castDesc | RDCSSFlag;
                                }
                                if (m != 0)
                                {
                                    desc.isKCAS = true;
                                    castDesc = castDesc | PMwCASFlag;
                                }
                                if (n != 0)
                                {
                                    desc.isDirty = true;
                                    castDesc = castDesc | DirtyFlag;
                                }
                                castBackDesc = (DescRef)castDesc;
                                if (desc != castBackDesc)
                                {
                                    std::cout << "Something is wrong with our custom type casting!" << std::endl;
                                }
                                assert(desc == castBackDesc);
                            }
                        }
                    }
                }
            }
        }
    };
    // Each KCAS Descriptor consists of K words.
    // Each Word Descriptor has a word, plus extra auxiliary information, such as the sequence number.
    struct alignas(8) Word
    {
        std::atomic<T> *address;
        T oldVal;
        T newVal;
        // Used for sorting Words.
        bool operator<(const Word &word) const
        {
            return ((uintptr_t)address < (uintptr_t)word.address);
        }
    };
    struct alignas(8) WordDescriptor : Word
    {
        struct Mutable
        {
        private:
            // Flag reserved to persist the whole descriptor.
            static const size_t isDirtySize = 1;
            // This will use whatever leftover bits remain while keeping the mutable word-sized.
            static const size_t seqSize = 64 - isDirtySize;

        public:
            // The dirty bit. Must be cleared before access to the rest of the descriptor is valid.
            bool isDirty : isDirtySize;
            // The sequence number of the descriptor.
            unsigned long seq : seqSize;

            operator uintptr_t() const
            {
                assert(sizeof(uintptr_t) == sizeof(Mutable));
                return (uintptr_t)(((uintptr_t)seq << (isDirtySize)) +
                                   ((uintptr_t)isDirty));
            }
            Mutable() noexcept {}
            Mutable(uintptr_t ptr)
            {
                assert(sizeof(uintptr_t) == sizeof(DescRef));
                seq = (unsigned long)(((size_t)ptr >> (isDirtySize)) & (((size_t)1 << seqSize) - 1));
                isDirty = (bool)(((size_t)ptr >> 0) & (((size_t)1 << isDirtySize) - 1));
            }
        };
        // A mutable, stored in 64 bits.
        std::atomic<WordDescriptor::Mutable> mutables;

        // Just enough information to associate a KCAS descriptor with this Word descriptor.
        unsigned long KCASseq;
        unsigned short KCAStid;

        WordDescriptor()
        {
            assert(std::atomic_is_lock_free(&mutables));
        }
    };
    struct alignas(8) KCASDescriptor
    {
        struct Mutable
        {
        private:
            // Flag reserved to persist the whole descriptor.
            static const size_t isDirtySize = 1;
            // Status of the KCAS as a whole.
            static const size_t statusSize = 2;
            // This will use whatever leftover bits remain while keeping the mutable word-sized.
            static const size_t seqSize = 64 - statusSize - isDirtySize;

        public:
            // The dirty bit. Must be cleared before access to the rest of the descriptor is valid.
            bool isDirty : isDirtySize;
            // The status of the KCAS.
            Status status : statusSize;
            // The sequence number of the descriptor.
            unsigned long seq : seqSize;

            operator uintptr_t() const
            {
                assert(sizeof(uintptr_t) == sizeof(Mutable));
                return (uintptr_t)(((uintptr_t)seq << (statusSize + isDirtySize)) +
                                   ((uintptr_t)status << (isDirtySize)) +
                                   ((uintptr_t)isDirty));
            }
            Mutable() noexcept {}
            Mutable(uintptr_t ptr)
            {
                assert(sizeof(uintptr_t) == sizeof(DescRef));
                seq = (unsigned long)(((size_t)ptr >> (statusSize + isDirtySize)) & (((size_t)1 << seqSize) - 1));
                status = (Status)(((size_t)ptr >> isDirtySize) & (((size_t)1 << statusSize) - 1));
                isDirty = (bool)(((size_t)ptr >> 0) & (((size_t)1 << isDirtySize) - 1));
            }
        };
        // A mutable, stored in 64 bits.
        std::atomic<KCASDescriptor::Mutable> mutables;

        // The number of words modified by the KCAS.
        size_t count;
        // The array of words modified by the KCAS.
        // NOTE: Everything beyond count elements are unused.
        alignas(8) Word words[K];

        KCASDescriptor()
        {
            assert(std::atomic_is_lock_free(&mutables));
        }
    };
    // Our descriptor pools.
    // TODO: Recover these in a persistent environment.
    // This simply involes saving the pools to file in NVM, using mmap.
    KCASDescriptor KCASDescriptors[P];
    WordDescriptor wordDescriptors[P];

    // TODO: Optionally recover from file.
    PMwCASManager(uintptr_t baseAddress = NULL,
                  bool reconstruct = false,
                  const char *fileName = NULL)
    {
        // P and K should always be a power of 2.
        assert((P & (P - 1)) == 0);
        assert((K & (K - 1)) == 0);
        // These types should be lock-free.
        assert(std::atomic<T>{}.is_lock_free());
        assert(std::atomic<DescRef>{}.is_lock_free());

        // Set the base address.
        // If it's NULL, then our offsets are just raw addresses.
        this->baseAddress = baseAddress;

        // If a file name is specified, attempt to recover.
        if (reconstruct && fileName != NULL)
        {
            //recover(fileName);
            return;
        }

        // TODO: Otherwise, just map our descriptors to file.

        // Initialize the KCAS descriptors.
        // TODO: Change this behavior to support recovery.
        for (size_t i = 0; i < P; i++)
        {
            typename KCASDescriptor::Mutable m;
            // Marked as dirty for consistency.
            m.isDirty = true;
            // Initial status doesn't really matter.
            m.status = Succeeded;
            // We make sure all sequence numbers start at 0.
            // This way, it starts out even.
            m.seq = 0;

            KCASDescriptors[i].mutables.store(m);
            KCASDescriptors[i].count = 0;
        }
        // Initialize the Word descriptors.
        for (size_t i = 0; i < P; i++)
        {
            typename WordDescriptor::Mutable m;
            // Marked as dirty for consistency.
            m.isDirty = true;
            // Though it doesn't particularly matter, we make sure all sequence numbers start at 0.
            m.seq = 0;

            wordDescriptors[i].mutables.store(m);
        }
        return;
    }
    // Gets the sequnce number regardless of descriptor type.
    unsigned long getSeq(DescRef desc)
    {
        if (desc.isKCAS && !desc.isRDCSS)
        {
            return pcas_read(&(KCASDescriptors[desc.tid].mutables)).seq;
        }
        else if (!desc.isKCAS && desc.isRDCSS)
        {
            return pcas_read(&(wordDescriptors[desc.tid].mutables)).seq;
        }
        else
        {
            throw std::runtime_error("Requested a sequence number for an invalid descriptor reference");
            return 0;
        }
    }
    // KCAS Descriptor generation.
    DescRef createNew(size_t threadNum, size_t size, Word *words)
    {
        assert(threadNum < P);
        assert(0 < size && size <= K);

        KCASDescriptor *desc = &(KCASDescriptors[threadNum]);
        // Incrementing the sequence number will invalidate this descriptor.
        typename KCASDescriptor::Mutable m = pcas_read<typename KCASDescriptor::Mutable>(&desc->mutables);
        // Update the sequnce number so our descriptor is considered new.
        m.seq++;
        m.status = Undecided;
        m.isDirty = true;
        desc->mutables.store(m);
        persist(&desc->mutables, m);
        // Now other threads cannot read this descriptor that is under construction.

        // Sort the words.
        std::sort(words, words + size);
        // Make sure the words are sorted correctly.
        for (size_t i = 0; i < size - 1; i++)
        {
            assert(((uintptr_t)words[i].address) < ((uintptr_t)words[i + 1].address));
        }

        // Construct the new descriptor.
        desc->count = size;
        for (size_t i = 0; i < size; i++)
        {
            desc->words[i] = words[i];
        }

        // Validate the descriptor.
        assert((desc->mutables.load().status == Undecided));
        assert(0 < desc->count && desc->count <= K);
        for (size_t j = 0; j < desc->count; j++)
        {
            assert(desc->words[j].address != NULL);
        }

        // Flush the descriptor.
        PERSIST_FLUSH_ONLY(desc, sizeof(KCASDescriptor));
        // Update the sequence number again.
        m.seq++;
        m.isDirty = true;
        desc->mutables.store(m);
        // Now flush the mutables and fence everything.
        persist(&desc->mutables, m);
        // Now this descriptor is valid and readable.

        // Return a descriptor reference.
        DescRef ref;
        ref.tid = threadNum;
        ref.seq = m.seq;
        // We make limited assumptions here. Dirty flag may need to be set before use.
        ref.isRDCSS = false;
        ref.isKCAS = true;
        ref.isDirty = false;
        return ref;
    }
    // RDCSS Descriptor generation.
    DescRef createNew(size_t helpingThreadNum, Word word, DescRef KCASDesc)
    {
        // localThreadNum: A thread_local value. Determines which thread to make descriptors on.
        assert(localThreadNum < P);
        // helpingThreadNum: The thread number of the thread in charge of this KCAS.
        assert(helpingThreadNum < P);

        WordDescriptor *desc = &(wordDescriptors[localThreadNum]);
        // Incrementing the sequence number will invalidate this descriptor.
        typename WordDescriptor::Mutable m = pcas_read<typename WordDescriptor::Mutable>(&desc->mutables);
        // Update the sequnce number so our descriptor is considered new.
        m.seq++;
        m.isDirty = true;
        desc->mutables.store(m);
        persist(&desc->mutables, m);
        // Now other threads cannot read this descriptor that is under construction.

        // Construct the new descriptor.
        desc->address = word.address;
        desc->oldVal = word.oldVal;
        desc->newVal = word.newVal;

        // This descriptor is inherently tied to the associated KCAS descriptor.
        desc->KCASseq = KCASDesc.seq;
        desc->KCAStid = helpingThreadNum;

        // Validate the descriptor.
        assert(desc->address != NULL);

        // Flush the descriptor.
        PERSIST_FLUSH_ONLY(desc, sizeof(WordDescriptor));
        // Update the sequence number again.
        m.seq++;
        m.isDirty = true;
        desc->mutables.store(m);
        persist(&desc->mutables, m);
        // Now this descriptor is valid and readable.

        // Return a descriptor reference.
        DescRef ref;
        ref.tid = localThreadNum;
        ref.seq = m.seq;
        // We make limited assumptions here. Dirty flag may need to be set before use.
        ref.isRDCSS = true;
        ref.isKCAS = false;
        ref.isDirty = false;
        return ref;
    }
    // These functions must support address (std::atomic<T> *) and mutables (std::atomic<KCASDescriptor::Mutable> *)
    template <class U>
    U readField(DescRef desc, std::atomic<U> *field, bool &success)
    {
        U result = pcas_read(field);
        // Compare our descriptor reference sequence number against the actual sequence number in the descriptor itself.
        if (desc.seq != getSeq(desc))
        {
            success = false;
            // NOTE: We shouldn't actually read the result in this case, but we need to return something.
            // Always check success to validate first.
        }
        return result;
    }
    template <class U>
    bool writeField(DescRef desc, U value, std::atomic<U> *field, bool &success)
    {
        while (true)
        {
            U expVal = pcas_read(field);

            // Compare our descriptor reference sequence number against the actual sequence number in the descriptor itself.
            if (desc.seq != getSeq(desc))
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
    bool CASField(DescRef desc, U &fExp, U fNew, std::atomic<U> *field, bool &success)
    {
        U expVal = pcas_read<U>(field);
        if (desc.seq != getSeq(desc))
        {
            success = false;
            return false;
        }
        if (expVal != fExp)
        {
            fExp = expVal;
            return false;
        }
        // pcas will ensure field is persisted and marks fNew as dirty autoamtically.
        bool CAS = pcas<U>(field, fExp, fNew);
        return CAS;
    }

    // Used to hide descriptor construction.
    // Format: address, oldVal, newVal
    bool PMwCAS(size_t threadNum, size_t size, Word *words)
    {
        assert(size > 0);
        // Create a "new" descriptor.
        DescRef desc = createNew(threadNum, size, words);

        // Run PMwCAS on our generated descriptor.
        bool ret = PMwCAS(desc);

        return ret;
    }
    // Perform a persistent, multi-word CAS based on a descriptor pointer.
    bool PMwCAS(DescRef desc, std::atomic<T> *addr = NULL)
    {
        // Fixed traversal order lets us skip inserting descriptors that have already been placed.
        size_t start = 0;
        if (addr != NULL)
        {
            for (size_t i = 0; i < KCASDescriptors[desc.tid].count; i++)
            {
                if (KCASDescriptors[desc.tid].words[i].address == addr)
                {
                    // Start on the next word.
                    start = i + 1;
                    break;
                }
            }
        }

        // The status we will assign to our KCAS. Defaults to success unless changed.
        Status st = Succeeded;
        // Must operate in a fixed address traversal order.
        // This is already handled on descriptor construction.
        for (size_t i = start; i < KCASDescriptors[desc.tid].count; i++)
        {
        retry:
            // Create a word descriptor.
            DescRef wordDesc = createNew(desc.tid, KCASDescriptors[desc.tid].words[i], desc);

            // Attempt to place the descriptor using RDCSS.
            bool success = true;
            T rval = InstallMwCASDescriptor(wordDesc, success);
            // If our descriptor is outdated, the PMwCAS has already finished (succeeded or failed).
            if (!success)
            {
                break;
            }
            // If the installation succeeded.
            if (rval == wordDescriptors[wordDesc.tid].oldVal)
            {
                // Continue to the next word.
                continue;
            }
            // If it failed because of another PMwCAS in progress.
            else if (((DescRef)rval).isKCAS)
            {
                // If the value stored there has not yet been persisted.
                if (((DescRef)rval).isDirty)
                {
                    // Persist it.
                    persist(KCASDescriptors[desc.tid].words[i].address, rval);
                }
                // If we clash with the KCAS descriptor we wanted to place anyway (via helping).
                if (((DescRef)rval).tid == desc.tid &&
                    ((DescRef)rval).seq == desc.seq)
                {
                    // Just move on to the next word.
                    continue;
                }
                // We clashed with a PMwCAS. Help complete it.
                PMwCAS((DescRef)rval, KCASDescriptors[desc.tid].words[i].address);
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

        // If we (may have) successfully inserted descriptors at all locations.
        if (st == Succeeded)
        {
            // Persist all target words.
            for (size_t i = 0; i < KCASDescriptors[desc.tid].count; i++)
            {
                // This will effectively persist all locations.
                // NOTE: This will persist the appropriate locations, even if something other than our expected KCAS descriptors are present.
                // This shouldn't be a problem. We know we succeeded.
                // At worst, this is persisting subsequent operations.
                // The conditional nature of pcas_read should make it low-overhead though.
                pcas_read(KCASDescriptors[desc.tid].words[i].address);
            }
        }

        // Finalize the status of the PMwCAS, whether success or failure.
        bool success = true;
        // The expected Mutable.
        typename KCASDescriptor::Mutable mOld;
        mOld.isDirty = false;
        mOld.status = Undecided;
        mOld.seq = desc.seq;
        // The updated Mutable.
        typename KCASDescriptor::Mutable mNew;
        mNew.isDirty = true;
        mNew.status = st;
        mNew.seq = desc.seq;
        // Try to update the Mutable.
        CASField<typename KCASDescriptor::Mutable>(desc, mOld, mNew, &KCASDescriptors[desc.tid].mutables, success);
        // Persist the Mutable. Only happens if we succeeded.
        persist(&KCASDescriptors[desc.tid].mutables, mNew);
        // If the descriptor changed from under us, then the PMwCAS is already over.
        if (!success)
        {
            return false;
        }

        // Install the final values.
        // DEBUG: Used to check if CAS failed.
        bool CAS1 = true;
        bool CAS2 = true;
        for (size_t i = 0; i < KCASDescriptors[desc.tid].count; i++)
        {
            // If we succeeded, we will place the new values.
            // If we failed, we will restore the old values.
            T v = ((st == Succeeded)
                       ? KCASDescriptors[desc.tid].words[i].newVal
                       : KCASDescriptors[desc.tid].words[i].oldVal);
            // We expect a PMwCAS descriptor that hasn't persisted.
            T expected = (T)((uintptr_t)desc | PMwCASFlag | DirtyFlag);
            T rval = (T)expected;
            // Replace it with a value.
            // CASField will automatically mark it as dirty.
            CAS1 = CASField<T>(desc, rval, v, KCASDescriptors[desc.tid].words[i].address, success);
            // Descriptor changed. Work must be complete.
            if (!success)
            {
                break;
            }

            // If we failed because the descriptor was already persisted.
            if ((uintptr_t)rval == ((uintptr_t)desc | PMwCASFlag))
            {
                // Try again, assuming that the descriptor *is* persisted.
                rval = (T)((uintptr_t)expected & ~DirtyFlag);
                CAS2 = CASField<T>(desc, rval, v, KCASDescriptors[desc.tid].words[i].address, success);
                // Descriptor changed. Work must be complete.
                if (!success)
                {
                    break;
                }
            }
            // Persist any change that occurs.
            persist(KCASDescriptors[desc.tid].words[i].address, v);

            // DEBUG: Counters
            if (desc.tid != localThreadNum && (CAS1 || CAS2))
            {
                helps++;
            }
            else if (desc.tid == localThreadNum && (CAS1 || CAS2))
            {
                opsDone++;
            }
        }
        // Return our final success (or failure).
        assert(st != Undecided);
        return (st == Succeeded);
    }
    // Attempt to read an address.
    // We must ensure all flag conditions have been handled before reading the address.
    T PMwCASRead(std::atomic<T> *address)
    {
        while (true)
        {
            // Read the value.
            // NOTE: Don't use pcas_read here.
            // Dirty bits are handled in our function.
            T v = address->load();
            // If it's part of an RDCSS.
            if ((uintptr_t)v & RDCSSFlag)
            {
                assert(((DescRef)v).tid != localThreadNum);
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

private:
    // Use RDCSS to replace a value with a descriptor.
    // desc should reference a valid tid and fieldID to identify the correct word descriptor.
    T InstallMwCASDescriptor(DescRef desc, bool &success)
    {
        T val;
        while (true)
        {
            // This old value could be associated with a newer descriptor.
            // CASField will fail if the sequence numbers don't match, so problems with using a newer descriptor by accident will be handled.
            T oldVal = wordDescriptors[desc.tid].oldVal;
            // This will be replaced with whatever was actually in the address field when we did the CAS.
            val = oldVal;
            // Replace with an RDCSS Descriptor.
            bool CAS = CASField<T>(desc, val, (T)desc, wordDescriptors[desc.tid].address, success);
            // One of these cases will always be true.
            assert((CAS && (val == oldVal)) ||
                   (!CAS && (val != oldVal)) ||
                   (!CAS && !success));
            // CAS can fail if the sequence numbers didn't match.
            if (!success)
            {
                return val;
            }
            // If the value was an RDCSS descriptor.
            if (((DescRef)val).isRDCSS)
            {
                // Help finish the RDCSS.
                CompleteInstall((DescRef)val);
                // Try the installation again.
                continue;
            }
            // If the value matched what we had expected (success, by us or someone else).
            if (val == oldVal)
            {
                // Finish the installation of our own descriptor.
                CompleteInstall(desc);
            }

            break;
        }
        // return the value previously in the target address.
        return val;
    }
    // Complete the RDCSS operation.
    bool CompleteInstall(DescRef RDCSSdesc)
    {
        bool success = true;
        // We create a dummy descriptor reference so we can verify the KCAS sequence number hasn't changed.
        // It is also sufficient for actual placement in the data structure.
        DescRef KCASDescDummy;
        KCASDescDummy.isDirty = false;
        KCASDescDummy.isKCAS = true;
        KCASDescDummy.isRDCSS = false;
        KCASDescDummy.seq = wordDescriptors[RDCSSdesc.tid].KCASseq;
        KCASDescDummy.tid = wordDescriptors[RDCSSdesc.tid].KCAStid;
        // Determine whether we are placing the KCAS descriptor or restoring the old value.
        bool u = (readField<typename KCASDescriptor::Mutable>(
                      KCASDescDummy,
                      &KCASDescriptors[wordDescriptors[RDCSSdesc.tid].KCAStid].mutables,
                      success)
                      .status == Undecided);
        // Mismatch in sequence numbers just means the owning thread finished this already.
        if (!success)
        {
            // If the sequence number has changed, the KCAS desciptor is already gone.
            // This means placing the KCAS will get it stuck in the data structure.
            // Instead, revert the RDCSS.
            // This could have only happened if we are a helper that wrongly succeeded in placing the RDCSS earlier, because the old value happened to be the same.
            // This is an instance of the ABA problem.
            // To revert, set u to false.
            u = false;
        }
        // Attempt the CAS. If we fail, it just means some other thread succeeded.
        T expected = (T)RDCSSdesc;
        // This should be always set already. Check to make sure.
        assert(((uintptr_t)expected & RDCSSFlag) != 0);
        T oldVal = wordDescriptors[RDCSSdesc.tid].oldVal;
        return CASField<T>(RDCSSdesc, expected, u ? (T)KCASDescDummy : oldVal, wordDescriptors[RDCSSdesc.tid].address, success);
    }

    // TODO: Implement this.
    // TODO: For the hash map, upon recovery, look for descriptors in the hash map and complete them.
    void recover()
    {
    }
};

#endif