#ifndef __RMATTEST_HEADER__
#define __RMATTEST_HEADER__

#include <assert.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

// Used to set process priority in Linux.
#include <sys/resource.h>
#include <unistd.h>

#include "define.hpp"
#include "hash.hpp"
#include "./cliff-map/hashMap.hpp"

// The cardinality of the graph.
inline const size_t NODE_COUNT = 2048;
inline const size_t RMAT_COUNT = 4;

typedef std::chrono::nanoseconds TIME_UNIT;

typedef unsigned long Key;
typedef unsigned long Value;

ConcurrentHashMap<Key, Value, xxhash<Key>> *hashMap;
// Create a pool of valid pointers.
// Threads can and should share these, to encourage overlaps.
size_t pointerPool[NODE_COUNT];

void threadRunner(std::thread *threads, void function(int threadNum));

void preinsert(int threadNum);

int setMaxPriority();

#endif