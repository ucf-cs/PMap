#ifndef DEFINE_hpp
#define DEFINE_hpp

#include <atomic>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <unordered_set>
#include <thread>

inline const size_t THREAD_COUNT = 2;
inline const size_t ARRAY_SIZE = 1;
inline const size_t NUM_OPS = 1000000;

typedef std::chrono::nanoseconds TIME_UNIT;

// A test array. We attempt to perform PMwCAS on the words contained within this array.
alignas(8) std::atomic<uintptr_t> array[ARRAY_SIZE];

thread_local size_t localThreadNum;
thread_local size_t helps = 0;
thread_local size_t opsDone = 0;

#endif