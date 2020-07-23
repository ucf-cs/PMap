#ifndef __MAIN_HEADER__
#define __MAIN_HEADER__

#include <assert.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

// Used to set process priority in Linux.
#include <sys/resource.h>
#include <unistd.h>

#include "define.hpp"
#include "hash.hpp"
#include "hashMap.hpp"

typedef std::chrono::nanoseconds TIME_UNIT;

typedef void *Key;
typedef void *Value;

ConcurrentHashMap<Key, Value, xxhash<Key>> *hashMap;
// Create a pool of valid pointers.
// Threads can and should share these, to encourage overlaps.
size_t pointerPool[PTR_POOL_SIZE];

void threadRunner(std::thread *threads, void function(int threadNum));

void preinsert(int threadNum);

int setMaxPriority();

#endif