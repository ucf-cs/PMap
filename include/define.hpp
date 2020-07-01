#ifndef DEFINE_HPP
#define DEFINE_HPP

#define NUM_OPS 10000
#define THREAD_COUNT 8

// Adjust this to artificially increase or decrease contention.
#define PTR_POOL_SIZE (THREAD_COUNT * NUM_OPS)

// Pointer marking.
// Offset can be set from 0-2 to mark different bits.
#define SET_MARK(_p, offset) (void *)((uintptr_t)_p | (uintptr_t)(1 << offset))
#define CLR_MARK(_p, offset) (void *)((uintptr_t)_p & (uintptr_t) ~(1 << offset))
#define IS_MARKED(_p, offset) (void *)((uintptr_t)_p & (uintptr_t)(1 << offset))

#endif