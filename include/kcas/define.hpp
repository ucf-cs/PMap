#ifndef DEFINE_hpp
#define DEFINE_hpp

#include <atomic>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <set>
#include <thread>

#define THREAD_COUNT 2
#define ARRAY_SIZE 32
#define NUM_OPS 10000
#define TIME_UNIT nanoseconds

alignas(64) std::atomic<uintptr_t> array[ARRAY_SIZE];

#endif