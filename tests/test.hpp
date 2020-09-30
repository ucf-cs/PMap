// This header defines all functions required by all tests to support the testing harness.
#ifndef TEST_HPP
#define TEST_HPP

#include <signal.h>

#include "runTest.hpp"

class Test
{
public:
    static std::atomic<size_t> waiting_threads;
    // Waits until all threads have been created and are ready to run.
    static void sync_start()
    {
        assert(waiting_threads.load());
        waiting_threads.fetch_add(-1);

        while (waiting_threads.load())
        {
        }
    }

    // Runs on the main thread at the beginning of a test.
    // Commonly used to pre-fill or initialize the test in some way.
    virtual void container_test_prefix(ThreadInfo &ti) = 0;
    // The actual test, typically run in parallel.
    virtual void container_test(ThreadInfo &ti) = 0;
    // Runs on the main thread at the end of a test.
    // Commonly used to consolidate results.
    virtual void container_test_suffix(ThreadInfo &ti) = 0;
    // The function run by each thread.
    // Typically includes container_test, but can have more if needed.
    static void ptest(Test *test, ThreadInfo &ti, time_point &starttime)
    {
        test->sync_start();
        starttime = std::chrono::system_clock::now();
        test->container_test(ti);
    }
    // A test-specific consistency check.
    virtual bool consistency_check(__attribute__((unused)) Test *test,
                                   __attribute__((unused)) const TestOptions &opt)
    {
        printf("No consistency check defined for this test. Assuming consistent.\n");
        return true;
    }
    // The number of operations that a thread will carry out
    static size_t opsPerThread(size_t numThreads, size_t totalOps, size_t threadID)
    {
        assert(threadID < numThreads);

        size_t numOps = totalOps / numThreads;
        size_t remOps = totalOps % numThreads;

        if (remOps > 0 && threadID == 0)
            numOps += remOps;

        return numOps;
    }
    /// computes number of operations in the main loop; the remainder of
    ///   operations will be executed prior to the main loop (i.e., insert elements)
    static size_t opsMainLoop(size_t numops)
    {
        return numops - (numops / 10);
    }

    [[noreturn]] void simulate_catastrophic_failure()
    {
        if (KILL_HARD)
            kill(getpid(), SIGKILL);

        std::abort();
    }

    void timed_catastrophe(int delay)
    {
        while (waiting_threads.load())
        {
        }

        std::this_thread::sleep_for(std::chrono::seconds(delay));

        simulate_catastrophic_failure();
    }
};

std::atomic<size_t> Test::waiting_threads;

#endif