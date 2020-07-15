#ifndef __MAIN_HEADER__
#define __MAIN_HEADER__

#define TIME_UNIT nanoseconds

#include <assert.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

// Used to set process priority in Linux.
#include <sys/resource.h>
#include <unistd.h>

#include "define.hpp"
#include "hashMap.hpp"

#define TABLE_SIZE 65536

ConcurrentHashMap<size_t, size_t> *hashMap;

void threadRunner(std::thread *threads, void function(int threadNum));

void preinsert(int threadNum);

int setMaxPriority();

#endif