#ifndef PERSISTENCE_HPP
#define PERSISTENCE_HPP

#include <atomic>
#include <cstdint>

#include <immintrin.h>

#include "marking.hpp"

#define DURABLE
#define PWB_IS_CLFLUSH

#ifdef DURABLE
#if defined PWB_IS_CLFLUSH
#define FLUSH(uptr) _mm_clflush((char *)uptr)
#elif defined PWB_IS_CLFLUSHOPT
#define FLUSH(uptr) pmem_clflushopt((char *)uptr)
#elif defined PWB_IS_CLWB
#define FLUSH(uptr) pmem_clwb((char *)uptr)
#endif
#define FENCE _mm_sfence();
#else
// Noop these instructions.
#define SFENCE
#define FLUSH(p)
#endif

const uintptr_t FLUSH_ALIGN = 64;

// Base persistence functions.
// TODO: Consider relocating these base functions to another file.
__attribute__((unused))
static void PERSIST(const void *addr, size_t len)
{
#ifdef DURABLE
    uintptr_t uptr;
    // Loop through cache-line-size (typically 64B) aligned chunks covering the given range.
    for (uptr = (uintptr_t)addr & ~(FLUSH_ALIGN - 1);
         uptr < (uintptr_t)addr + len;
         uptr += FLUSH_ALIGN)
    {
        FLUSH(uptr);
    }
    FENCE;
#endif
}

__attribute__((unused))
static void PERSIST_FLUSH_ONLY(const void *addr, size_t len)
{
#ifdef DURABLE
    uintptr_t uptr;
    // Loop through cache-line-size (typically 64B) aligned chunks covering the given range.
    for (uptr = (uintptr_t)addr & ~(FLUSH_ALIGN - 1);
         uptr < (uintptr_t)addr + len;
         uptr += FLUSH_ALIGN)
    {
        FLUSH(uptr);
    }
#endif
}

__attribute__((unused))
static void PERSIST_BARRIER_ONLY()
{
#ifdef DURABLE
    FENCE;
#endif
}

// Flush and fence the data stored in the address, then unflag the dirty bit atomically.
template <class U>
static U persist(std::atomic<U> *address, U value)
{
    FLUSH(address);
    FENCE;
    address->compare_exchange_strong(value, (U)((uintptr_t)value & ~DirtyFlag));
    return value;
}

// Always use this when reading fields with a dirty flag.
// This includes DescRef, T, WordDescriptor::Mutable, and KCASDescriptor::Mutable.
// NOTE: All of these data types should be dirty on initialization, just for safety.
template <class U>
static U pcas_read(std::atomic<U> *address)
{
    U word = address->load();
    if ((word & DirtyFlag) != 0)
    {
        persist(address, word);
    }
    return word & ~DirtyFlag;
}

// Always use this when CAS'ing fields with a dirty flag, unless you are marking the dirty flag by hand.
// This includes DescRef, T, WordDescriptor::Mutable, and KCASDescriptor::Mutable.
template <class U>
static bool pcas(std::atomic<U> *address, U &oldVal, U newVal)
{
    U oldValCopy = oldVal;
    // Ensure the field is persisted.
    pcas_read<U>(address);
    // Attempt to CAS.
    bool ret = address->compare_exchange_strong(oldVal, newVal | DirtyFlag);
    assert((ret && oldValCopy == oldVal) || (!ret && oldValCopy != oldVal));
    return ret;
}

#endif
