// Desired interface:
// DescriptorType *ptr = prov.allocateKcasDesc(tid);
// ptr->numEntries = KCAS_MAXK;
// for (int i = 0; i < ptr->numEntries; ++i)
// {
//     casword_t *addr = &data[ix[i]];
//     casword_t oldval = prov.readVal(tid, &data[ix[i]]);
//     casword_t newval = oldval + 1;
//     ptr->entries[i].addr = addr;
//     ptr->entries[i].oldval = oldval << KCAS_LEFTSHIFT;
//     ptr->entries[i].newval = newval << KCAS_LEFTSHIFT;
// }

// // invoke the actual kcas (which will clean up the kcas descriptor eventually)
// return prov.kcas(tid, ptr);

// Notes:
// Important figures: 6, 4, 2 (K-CAS), 1 (RDCSS)
// Mutable descriptor: Like an immutable descriptor, but can additionally WriteField and CASField.
//    Our descriptors should be entirely immutable?
//  Weak descriptor: Each time a process invokes CreateNew to create a new descriptor, it invalidates all of its previous descriptors.
// Instead of storing descriptor pointers, store a process name and sequence number.
// Potential memory reclaimation for our arrays:
//    epoch-based reclamation [7]
//    hazard pointers [26]
//    read-copy-update (RCU) [13]

#include <atomic>
#include <utility>
#include <cstddef>

#include "../define.hpp"

// T: The data type being modified by the KCAS.
// K: The maximum number of elements that can be modified by a single descriptor.
// P: The number of processes (threads) that can perform KCAS.
template <class T, size_t K, size_t P>
struct KCAS
{
    // A reference to a descriptor.
    // Rather than using a marked pointer, we have a thread ID and sequence number.
    // This is enough information to retrieve our reusable descriptors while avoiding the ABA problem.
    struct DescRef
    {
        // Thread ID.
        // can only address 2^width-1 threads.
        unsigned int tid : 8;
        // A sequence number to avoid the ABA problem.
        unsigned int seq : 54;
        // Dummy bitfield to store mark.
        bool isRef : 1;
        // Dummy bitfield to store another mark.
        bool isCopy : 1;
    };

    // A KCAS descriptor.
    // Contains all data needed to perform helping as needed.
    struct Descriptor
    {
        enum state
        {
            undecided,
            succeeded,
            failed,
        };
        // Atomic state of the descriptor.
        std::atomic<state> state;
        // Addresses.
        std::atomic<T> *addr[K];
        // Expected values.
        T oldVal[K];
        // New values.
        T newVal[K];

        // The number of elements actually modified.
        // Must be <= K.
        size_t len;

        // Sequence number.
        std::atomic<size_t> seq;
    };

    // Our weak descriptor stores the actual descriptors and sentinel values.
    struct WeakDescriptor
    {
        // TODO: Make proper sentinel values for these.
        static DescRef dv;
        static T tdv;

        // The descriptors.
        // Threads reuse their own descriptors.
        Descriptor[P] descriptor;

        // TODO: This requires 3 arrays of T. Can I make these in advance for reuse? Deal with it later.
        DescRef createNew(size_t tid, size_t len, std::atomic<T> *addr, T *oldVal, T *newVal)
        {
            // Cannot perform more than K modifications in a KCAS.
            assert(len <= K);

            // Get the current sequence number.
            size_t oldSeq = descriptor[tid].seq.load();
            // Invalidate old uses of this descriptor.
            descriptor[tid].seq.store(oldSeq + 1);

            // Fill in descriptor values.
            for (size_t i = 0; i < len; i++)
            {
                descriptor[tid].addr[i] = addr[k];
                descriptor[tid].oldVal[i] = oldVal[k];
                descriptor[tid].newVal[i] = newVal[k];
            }
            descriptor[tid].len = len;
            // Invalidate this descriptor again.
            descriptor[tid].seq.store(oldSeq + 2);

            // Return a reference to this descriptor.
            DescRef ret;
            ret.tid = tid;
            ret.seq = oldSeq + 2;
            ret.isRef = true;
            return ret;
        }

        // TODO: Modify these to accomodate addr, oldVal, and newVal.

        // Read a field in a descriptor.
        T readField(DescRef desc, size_t fieldID)
        {
            assert(fieldID < K);
            result = descriptor[desc.tid].imm[fieldID]->load();
            if (desc.seq != descriptor[desc.tid].seq)
            {
                return dv;
            }
            return result;
        }

        // Write a field in a descriptor.
        bool writeField(DescRef desc, size_t fieldID, T value)
        {
            while (true)
            {
                T expVal = descriptor[desc.tid].imm[fieldID]->load();
                if (desc.seq != descriptor[desc.tid].seq)
                {
                    return false;
                }
                T newVal = value;
                if (CASField(desc, fieldID, expVal, newVal))
                {
                    return true;
                }
            }
        }

        // CAS a field in a descriptor.
        T CASField(DescRef desc, size_t fieldID, T fExp, T fNew)
        {
            while (true)
            {
                T expVal = descriptor[desc.tid].imm[fieldID]->load();
                if (desc.seq != descriptor[desc.tid].seq)
                {
                    return tdv;
                }
                if (expVal != fExp)
                {
                    return fExp;
                }
                T newVal = fNew;
                if (descriptor[desc.tid].imm[fieldID]->compare_exchange_strong(expVal, newVal))
                {
                    return newVal;
                }
            }
        }

        // Initialize our descriptors, sentinels, etc.
        WeakDescriptor()
        {
            // TODO: Initialize the sentinels.
            // TODO: Allocate the reusable copy arrays.

            // TODO: Initialize the descriptors?
            for (size_t i = 0; i < 2 * p; i++)
            {
                //new(&descriptor[i]) Descriptor();
            }
        }
    };

    class DCSS
    {
        // Check to see if the pointer is a descriptor.
        static bool IsDescriptor(uintptr_t r)
        {
            return IS_MARKED(r, 1);
        }

        // Complete a partial RDCSS.
        // All descriptors that come through here are probably marked.
        static void Complete(Descriptor *marked)
        {
            // Mark them anyway, just to be sure.
            Descriptor *expected = (Descriptor *)SET_MARK(marked, 1);

            // Clear the mark before use.
            Descriptor *d = (Descriptor *)CLR_MARK(marked, 1);

            // Get the value in the control section.
            uintptr_t v = d->a1->load(); // This is our linearization point for RDCSS, dependent on the thread completing a below CAS.

            // If the value in the descriptor for the control section still matches the expected value.
            if (v == d->o1)
            {
                // Remove the descriptor pointer in the data section, replacing it with the new value.
                // The only time CAS fails here is if another thread completed for us.
                d->a2->compare_exchange_strong(expected, d->n2);
            }
            else
            {
                // Remove the descriptor pointer in the data section, restoring the old value.
                // The only time CAS fails here is if another thread completed for us.
                d->a2->compare_exchange_strong(expected, d->o2);
            }
            return;
        }
        // Restricted Double-Compare Single-Swap.
        static uintptr_t RDCSS(Descriptor *d)
        {
            uintptr_t ret = NULL;
            while (true)
            {
                // We wil use this to store the old return, just like CAS normally does.
                // This can be used to indicate success or failure.
                ret = d->o2;
                // Change the data section to point to the descriptor.
                // This denies access to a2 until RDCSS has compelted.
                // We mark d so that threads can recognize this as a descriptor.
                d->a2->compare_exchange_strong(ret, (uintptr_t)(SET_MARK(d)));

                // If the original value was a descriptor.
                // Someone else must have inserted a descriptor before us.
                // We must have failed.
                if (IsDescriptor((uintptr_t)ret))
                {
                    // We must help it complete before trying again.
                    Complete((Descriptor *)ret);
                }
                // If we replaced a non-descriptor with a descriptor, break out of the loop.
                // We can never replace a descriptor with a descriptor because we never use descriptors as expected values.
                // If we failed to replace a non-descriptor with a descriptor, the CAS failed and ret doesn't match our expected value. We fail and break out of the loop.
                // If we failed to replace a descriptor with a descriptor, try again now that we have resolved the conflict.
                if (!IsDescriptor((uintptr_t)ret))
                {
                    break;
                }
            }

            // If the CAS succeded, try to complete our task.
            // If this evaluates false, it means ret doesn't match our expected value, so we fail.
            if (ret == d->o2)
            {
                // Failure here just means someone else did it for us.
                Complete((Descriptor *)SET_MARK(d));
            }

            // ret can be used to indicate success or failure.
            // ret == d->o2 indicates success.
            return ret;
        }

    public:
        // Build and pass in a descriptor object to store the descriptor fields.
        static uintptr_t RDCSS(std::atomic<state> *a1, state o1, std::atomic<T> *a2, T o2, T n2)
        {
            // TODO: Dynamic memory allocation here is bad, but these descriptors must be unique per-operation.
            // NOTE: Actually shown that descriptors can be reused.
            RDCSSDescriptor *desc = new RDCSSDescriptor;
            // 1: Control section
            desc->a1 = a1;
            desc->o1 = o1;
            // 2: Data section
            desc->a2 = a2;
            desc->o2 = o2;
            desc->n2 = n2;
            // Run the actual operation.
            return RDCSS(desc);
        }
        // RDCSS-aware Read.
        static uintptr_t RDCSSRead(std::atomic<uintptr_t> *addr)
        {
            uintptr_t r = NULL;
            do
            {
                // Get the value at the address.
                r = addr->load(); // This is the linearization point, once r is not a descriptor.
                // If it is a descriptor.
                if (IsDescriptor(r))
                {
                    // Help the descriptor to compelte or fail.
                    Complete((RDCSSDescriptor *)r);
                }
                // Keep helping until a non-descriptor value is loaded.
            } while (IsDescriptor(r));
            // Return the old value.
            return r;
        }
    };

    KCAS(size_t tid, size_t len, std::atomic<T> *addr, T *oldVal, T *newVal)
    {
        DescRef desc = createNew(tid, len, addr, oldVal, newVal);
        DescRef fDesc = SET_MARK(desc, 1);
        return KCASHelp(fDesc);
    }

    void KCASHelp(DescRef fdesc)
    {
        // Make sure the mark is cleared before using this.
        // It should actually be safe anyway since we reserve these spots with bitfields.
        DescRef desc = CLR_MRK(fdesc, 1);

        // If the descriptor state is still undecided.
        state state = descriptor[desc.tid].state.load();
        if (state == undecided)
        {
            state = succeeded;
            // Place the descriptor reference in each memory location.
            for (size_t i = 0; i < descriptor[desc.tid].len; i++)
            {
            retry:
            }
        }

        for (size_t i = 0; i < descriptor[desc.tid].len; i++)
        {
            T val = readField(fdesc, i);
            if (val == fdv)
            {
                return;
            }
            if ()
            {
            }
        }
    }
};