#ifndef DEFINE_HPP
#define DEFINE_HPP

#include <cstddef>
#include <iostream>
#include <string>
#include <typeinfo>

// Globally defined constants, functions, etc.

using time_point = std::chrono::time_point<std::chrono::system_clock>;
using duration_unit = std::chrono::milliseconds;

// The key and value datatypes.
// NOTE: Certain tests are only compatible with certain types.
// NOTE: Type must reserve the 3 least significant bits.
// NOTE: Type must reserve some values as sentinels.
// NOTE: Type must be 64 bits (perhaps fewer).
using KeyT = size_t;
using ValT = size_t;

#ifndef DEFAULT_CACHELINE_SIZE
static constexpr size_t CACHELINESZ = 64;
//static constexpr size_t CACHELINESZ = std::hardware_destructive_interference_size;
#else
static constexpr size_t CACHELINESZ = DEFAULT_CACHELINE_SIZE;
#endif

static constexpr bool KILL_HARD = true;
static constexpr bool ALWAYS_RUN_CONSISTENCY_CHECKS = false;

// Test options can be overridden by command line.
// The test itself has final say in which parameters are used.
struct TestOptions
{
    // Number of threads to spawn and use.
    size_t numthreads;
    // Number of operations to run, shared accross all threads.
    size_t numops;
    // Number of times to repeast the test.
    size_t numruns;
    // Starting (or total) capacity of the container.
    size_t capacity; // Actual capacity is 2 ^ capacity.
    // Location and name of the persistent file used.
    std::string filename;
    // Whether to perform a recovery test or run the normal test.
    bool recover;
    // Whether to wipe or recover the file.
    bool wipeFile;

    TestOptions();

    // TODO: Print out final arguments used.
    void print()
    {
        std::cout << "*** concurrent container test "
                  << "\n***          number of threads: " << numthreads
                  << "\n*** total number of operations: " << numops
                  << "\n***       total number of runs: " << numruns
                  << "\n***  initial capacity (base 2): " << capacity
                  << "\n***            actual capacity: " << (1 << capacity)
                  << "\n***                mapped file: " << filename
                  << "\n***                    recover: " << recover
                  << "\n***                  wipe file: " << wipeFile
                  //<< "\n***                  test type: " << typeid(test_type).name()
                  //<< "\n***             container type: " << typeid(container_type).name()
                  << std::endl;
        return;
    }
};

struct alignas(CACHELINESZ) ThreadInfo
{
    // container_type*
    void *container;

    size_t num;
    size_t fail;
    size_t succ;
    size_t pnoiter;
    size_t num_threads;
    size_t num_held_back;

    ThreadInfo(void *r, size_t n, size_t cntiter, size_t cntthreads)
        : container(r), num(n), fail(0), succ(0), pnoiter(cntiter), num_threads(cntthreads),
          num_held_back(0)
    {
        assert(num < num_threads);
    }

    ThreadInfo()
        : ThreadInfo(nullptr, 0, 0, 1)
    {
    }
};

#endif