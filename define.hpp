#ifndef DEFINE_HPP
#define DEFINE_HPP

#define NUM_OPS 10000
#define THREAD_COUNT 8

// Adjust this to artificially increase or decrease contention.
#define PTR_POOL_SIZE (THREAD_COUNT * NUM_OPS)

#endif