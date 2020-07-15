#ifndef DEFINE_hpp
#define DEFINE_hpp

#include <atomic>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <unordered_set>
#include <thread>

#define THREAD_COUNT 8
#define ARRAY_SIZE 16384
#define NUM_OPS 100000
#define TIME_UNIT nanoseconds

alignas(64) std::atomic<uintptr_t> array[ARRAY_SIZE];

#endif