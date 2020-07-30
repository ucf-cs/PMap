#ifndef DEFINE_hpp
#define DEFINE_hpp

#include <atomic>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <unordered_set>
#include <thread>

inline const size_t THREAD_COUNT = 8;
inline const size_t ARRAY_SIZE = 16384;
inline const size_t NUM_OPS = 100000;

typedef std::chrono::nanoseconds TIME_UNIT;

// A test array. We attempt to perform PMwCAS on the words contained within this array.
alignas(8) std::atomic<uintptr_t> array[ARRAY_SIZE];

#endif