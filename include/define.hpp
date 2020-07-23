#ifndef DEFINE_HPP
#define DEFINE_HPP

inline const size_t NUM_OPS = 10000;
inline const size_t THREAD_COUNT = 8;

// Adjust this to artificially increase or decrease contention.
inline const size_t PTR_POOL_SIZE = THREAD_COUNT * NUM_OPS;

// Pointer marking.
// Offset can be set from 0-2 to mark different bits.
inline void *setMark(uintptr_t p, size_t offset)
{
    return (void *)(p | (uintptr_t)(1 << offset));
}
inline void *clearMark(uintptr_t p, size_t offset)
{
    return (void *)(p & (uintptr_t) ~(1 << offset));
}
inline void *isMarked(uintptr_t p, size_t offset)
{
    return (void *)(p & (uintptr_t)(1 << offset));
}

#endif