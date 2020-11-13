#ifndef RUN_TEST_HPP
#define RUN_TEST_HPP

#include <cstddef>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <list>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// Only uncomment PMEM containers when you use them.
// Otherwise, they will try to open a PMEM file on persistent memory, even when unused.
//#include "containers/onefileMap.hpp"
//#include "containers/pmemMap.hpp"
//#include "containers/stlMap.hpp"
#include "containers/ucfMap.hpp"
//#include "containers/ucfHopscotchMap.hpp"
#include "containers/levelMap.hpp"

// The container to use.
//using container_type = ucf::container_type;
//using container_type = ucfHopscotch::container_type;
//using container_type = stl::container_type;
//using container_type = pm::container_type;
//using container_type = onefile::container_type;
using container_type = clevel::container_type;

#include "tests/alternating.hpp"
#include "tests/degree.hpp"
#include "tests/random.hpp"
#include "tests/reddit.hpp"

// The test to run.
//using test_type = alternatingTest::test_type;
//using test_type = degreeTest::test_type;
//using test_type = redditTest::test_type;
using test_type = randomTest::test_type;

TestOptions::TestOptions()
{
    numthreads = 8;
    numops = 40;
    numruns = 1;
    capacity = 16; // Actual capacity is 2 ^ capacity.
    filename = "/mnt/pmem/pm1/persist.bin";
    recover = true;
    wipeFile = false;
}

// This function sets an arbitrarily-sized value in some arbitrary memory location.
template <class T>
static void setField(T &fld, T val)
{
    fld = val;
}

template <class U, class V>
U conv(const V &val)
{
    std::stringstream tmp;
    U res;

    tmp << val;
    tmp >> res;
    return res;
}

template <class N, class T>
bool matchOpt1(const std::vector<std::string> &args, N &pos, std::string opt, T &fld)
{
    std::string arg(args.at(pos));
    if (arg.find(opt) != 0)
        return false;
    ++pos;
    fld = conv<T>(args.at(pos));
    ++pos;
    return true;
}

template <class N, class Fn, class... Parms>
bool matchOpt0(const std::vector<std::string> &args, N &pos, std::string opt, Fn fn, Parms... parms)
{
    std::string arg(args.at(pos));
    if (arg.find(opt) != 0)
        return false;
    fn(parms...);
    ++pos;
    return true;
}

static void help(const std::string &executable)
{
    TestOptions tmp;
    std::cout << "A test harness for associative containers: " << executable << std::endl
              << "test " << typeid(test_type).name() << std::endl
              << "container " << typeid(container_type).name() << std::endl
              << std::endl
              << "usage: " << executable << " [arguments]" << std::endl
              << std::endl
              << "arguments:" << std::endl
              << "-t num   number of threads (default: " << tmp.numthreads << ")\n"
              << "-n num   number of total operations executed (default: " << tmp.numops << ")\n"
              << "-p num   number of parallel runs (default: " << tmp.numruns << ")\n"
              << "-c num   sets initial container capacity to 2^num (default: " << tmp.capacity << ")\n"
              << "-f name  name of the mmaped file (default: " << tmp.filename << ")\n"
              << "-r bool  whether to run the recovery test or the main test (default: " << tmp.recover << ")\n"
              << "-w bool  whether to wipe or recover the persistent file (default: " << tmp.wipeFile << ")\n"
              << "-h       displays this help message\n"
              << std::endl;
    exit(0);
}

int run_test(test_type *test, const TestOptions &opt)
{
    std::cout << std::endl;

    std::list<std::thread> exp_threads;
    std::vector<ThreadInfo> thread_info(opt.numthreads, ThreadInfo{});
    container_type *contptr = new container_type(opt, opt.recover);

    ThreadInfo *tmpThreadInfo = new ThreadInfo(contptr, 0, opt.numops, opt.numthreads);
    test->container_test_prefix(*tmpThreadInfo);

    time_point starttime;

    // Used to start all threads at the same time.
    test->waiting_threads = opt.numthreads;

    // spawn
    for (size_t i = 0; i < opt.numthreads; ++i)
    {
        ThreadInfo &ti = thread_info.at(i);

        ti = ThreadInfo(contptr, i, opt.numops, opt.numthreads);
        exp_threads.emplace_back(&test_type::ptest, test, std::ref(ti), std::ref(starttime));
    }

    // join
    for (std::thread &thr : exp_threads)
        thr.join();

    time_point endtime = std::chrono::system_clock::now();
    int elapsedtime = std::chrono::duration_cast<duration_unit>(endtime - starttime).count();

    const int actsize = contptr->count();
    std::cout << "elapsed time = " << elapsedtime << "ms" << std::endl;
    std::cout << "container size = " << actsize << std::endl;

    std::cerr << elapsedtime << std::endl;

    test->container_test_suffix(*tmpThreadInfo);

    delete contptr;
    delete tmpThreadInfo;

    return elapsedtime;
}

// Test to ensure that the data structure persisted to a recoverable state.
int recovery_test(test_type *test, const TestOptions &opt)
{
    // Recover the container.
    container_type *contptr = new container_type(opt, true);
    // Container-specific consistency check.
    if (!contptr->isConsistent())
    {
        printf("Container is not consistent!\n");
    }
    // Test-specific consistency check.
    if (!test->consistency_check(test, opt))
    {
        printf("Test is not consistent!\n");
    }
    delete contptr;
    return 0;
}

#endif