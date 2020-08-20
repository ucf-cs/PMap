#ifndef TEST_FRAMEWORK
#define TEST_FRAMEWORK

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <list>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef DEFAULT_CACHELINE_SIZE
static constexpr size_t CACHELINESZ = 64;
//static constexpr size_t CACHELINESZ = std::hardware_destructive_interference_size;
#else
static constexpr size_t CACHELINESZ = DEFAULT_CACHELINE_SIZE;
#endif

#ifndef TEST_CONFLICTS
#define TEST_CONFLICTS 0
#endif

// NOTE: Change this and the include to change the data structure.
#define DATA_STRUCTURE UniqueElems

struct TestOptions
{
    size_t numthreads = 8;
    size_t numops = 40;
    size_t numruns = 1;
    size_t capacity = 16; // actual capacity is 2 ^ capacity
    bool sequential = false;
    bool printdata = false;
    std::string filename = "./chashmap.dat";
};

using KeyT = size_t;
using ValT = size_t;

#include "container-ucfMap.hpp"
#include "container-stlMap.hpp"
//#include "container-pmemMap.hpp"
//#include "container-onefileMap.hpp"

//
// define container and operations
// i.e., insert, erase, count

using container_type = ucf::container_type;
//using container_type = stl::container_type;
//using container_type = pm::container_type;
//using container_type = onefile::container_type;

// end define container

class TestFramework
{
public:
    static std::atomic<bool> ptest_failed; //(false);
    /// counts number of threads that are ready to run
    static std::atomic<size_t> waiting_threads;

protected:
    using duration_unit = std::chrono::milliseconds;
    using time_point = std::chrono::time_point<std::chrono::system_clock>;

    TestFramework()
    {
        ptest_failed.store(false);
    }
    struct alignas(CACHELINESZ) ThreadInfo
    {
        container_type *container;
        size_t num;
        size_t fail;
        size_t succ;
        size_t pnoiter;
        size_t num_threads;
        size_t num_held_back;
        // unsigned        cpunode_start;
        // unsigned        cpunode_end;
        // std::set        elemsin;
        // std::set        elemsout;

        ThreadInfo(container_type *r, size_t n, size_t cntiter, size_t cntthreads)
            : container(r), num(n), fail(0), succ(0), pnoiter(cntiter), num_threads(cntthreads),
              num_held_back(0)
        // , cpunode_start(0), cpunode_end(0)
        {
            assert(num < num_threads);
        }

        ThreadInfo()
            : ThreadInfo(nullptr, 0, 0, 1)
        {
        }
    };

    void fail()
    {
        throw std::runtime_error("error");
    }

    /// waits until all threads have been created and are ready to run
    static void sync_start()
    {
        assert(waiting_threads.load());
        waiting_threads.fetch_add(-1);

        while (waiting_threads.load())
        {
        }
    }

    /// computes number of operations that a thread will carry out
    static size_t opsPerThread(size_t maxthread, size_t totalops, size_t thrid)
    {
        assert(maxthread > thrid);

        size_t numops = totalops / maxthread;
        size_t remops = totalops % maxthread;

        if (remops > 0 && thrid < remops)
            ++numops;

        return numops;
    }

    /// computes number of operations in the main loop; the remainder of
    ///   operations will be executed prior to the main loop (i.e., insert elements)
    static size_t opsMainLoop(size_t numops)
    {
        return numops - (numops / 10);
    }

    /// computes the expected size after all operations have finished
    ///   requires deterministic container operations
    int _expectedSize(size_t maxthread, size_t totalops)
    {
        int elems = 0;

        for (size_t i = 0; i < maxthread; ++i)
        {
            int numops = opsPerThread(maxthread, totalops, i);
            int nummain = opsMainLoop(numops);

            // std::cerr << numops << "<o m>" << nummain << std::endl;

            // special case when nummain is even and small
            //   then the insert happens after the delete
            if ((numops - nummain == 0) && (nummain % 2 == 0))
                elems += numops / 2;
            else
                elems += (numops - nummain);

            // if odd, we insert one more
            if (nummain % 2)
                ++elems;
        }

        return elems;
    }

#if TEST_CONFLICTS

    size_t genElem(size_t num, size_t thrid, size_t maxthread, size_t)
    {
        return num * maxthread + thrid;
    }

    int expectedSize(size_t maxthread, size_t totalops)
    {
        return _expectedSize(maxthread, totalops);
    }

#else

    int expectedSize(size_t maxthread, size_t totalops)
    {
        return _expectedSize(maxthread, totalops);
    }

    static size_t genElem(size_t num, size_t thrid, size_t, size_t maxOpsPerThread)
    {
        assert(num < maxOpsPerThread);

        return thrid * maxOpsPerThread + num;
    }

#endif

    virtual void container_test_prefix(ThreadInfo &ti)
    {
        throw std::runtime_error("Called parent function instead of child instance.");
        return;
    }

    virtual void container_test(ThreadInfo &ti)
    {
        throw std::runtime_error("Called parent function instead of child instance.");
        return;
    }

    virtual void ptest(ThreadInfo &ti, time_point &starttime)
    {
        throw std::runtime_error("Called parent function instead of child instance.");
        return;
    }

public:
    size_t sequential_test(const TestOptions &opt)
    {
        container_type *const tag = nullptr;
        std::unique_ptr<container_type> cont{&construct(opt, tag)};
        ThreadInfo info{cont.get(), 0, opt.numops, 1};

        std::cout << std::endl;
        container_test_prefix(info);

        time_point starttime = std::chrono::system_clock::now();

        container_test(info);

        time_point endtime = std::chrono::system_clock::now();
        const int elapsedtime = std::chrono::duration_cast<duration_unit>(endtime - starttime).count();
        const int actsize = count(*cont);
        const int expctsize = expectedSize(1, opt.numops);

        if (expctsize != actsize)
        {
            std::cerr << "Unexpected size: "
                      << actsize << " <actual != expected> " << expctsize
                      << std::endl;
            fail();
        }

        std::cout << "elapsed time = " << elapsedtime << "ms" << std::endl
                  << "container size = " << actsize << std::endl
                  << "seq OK" << std::endl;

        return elapsedtime;
    }

    int parallel_test(const TestOptions &opt)
    {
        std::cout << std::endl;

        std::list<std::thread> exp_threads;
        std::vector<ThreadInfo> thread_info(opt.numthreads, ThreadInfo{});
        container_type *const tag = nullptr;
        std::unique_ptr<container_type> cont{&construct(opt, tag)}; // for lifetime management
        container_type *contptr = cont.get();
        time_point starttime;

        waiting_threads = opt.numthreads;

        // spawn
        for (size_t i = 1; i < opt.numthreads; ++i)
        {
            ThreadInfo &ti = thread_info.at(i);

            ti = ThreadInfo(contptr, i, opt.numops, opt.numthreads);
            exp_threads.emplace_back(&TestFramework::ptest, this, std::ref(ti), std::ref(starttime));
        }

        // use main thread as worker
        ThreadInfo &thisTi = thread_info.front();

        thisTi = ThreadInfo(contptr, 0, opt.numops, opt.numthreads);
        this->ptest(thisTi, starttime);

        // join
        for (std::thread &thr : exp_threads)
            thr.join();

        time_point endtime = std::chrono::system_clock::now();
        int elapsedtime = std::chrono::duration_cast<duration_unit>(endtime - starttime).count();

        __sync_synchronize();
        const int actsize = count(*cont); // std::distance(cont.qbegin(), cont.qend());
        std::cout << "elapsed time = " << elapsedtime << "ms" << std::endl;
        std::cout << "container size = " << actsize << std::endl;

        std::cerr << elapsedtime << std::endl;

        size_t total_succ = 0;
        size_t total_fail = 0;

        for (ThreadInfo &info : thread_info)
        {
            std::cout << ((info.succ == (info.fail + info.succ)) ? "   " : "!! ")
                      << std::setw(3) << info.num << ": "
                      << info.succ << " of (" << (info.fail + info.succ) << ")"
                      //~ << " [ " << thread_info[i].cpunode_start
                      //~ << " - " << thread_info[i].cpunode_end
                      //~ << " ]  x "
                      //~ << "  ( " << info.num_held_back << " obj held )"
                      << std::endl;

            total_succ += info.succ;
            total_fail += info.fail;
        }

        const int expctsize = expectedSize(opt.numthreads, opt.numops);

        if (expctsize != actsize)
        {
            std::cerr << "Unexpected size: "
                      << actsize << " <actual != expected> " << expctsize
                      << std::endl;
            fail();
        }

        std::cout << actsize << std::endl;
        std::cout << "par OK." << std::endl;

        return elapsedtime;
    }

    void prnGen(size_t numThreads, int nops)
    {
        std::vector<size_t> dataset;
        const size_t maxops = opsPerThread(numThreads, nops, 0);

        for (size_t tinum = 0; tinum < numThreads; ++tinum)
        {
            {
                size_t numops = opsPerThread(numThreads, nops, tinum);
                const size_t nummain = opsMainLoop(numops);
                size_t wrid = 0;

                std::cout << tinum << ": " << numops << "/" << nummain << std::endl;
                while (numops > nummain)
                {
                    dataset.push_back(genElem(wrid, tinum, numThreads, maxops));
                    ++wrid;
                    --numops;
                    // std::cout << ", i" << dataset.back();
                }
            }

            {
                std::cout << " - ";

                const size_t numops = opsPerThread(numThreads, nops, tinum);
                size_t nummain = opsMainLoop(numops);
                size_t wrid = numops - nummain;
                size_t rdid = wrid / 2;

                while (nummain)
                {
                    if (nummain % 2)
                    {
                        dataset.push_back(genElem(wrid, tinum, numThreads, maxops));
                        ++wrid;
                        // std::cout << ", i" << dataset.back();
                    }
                    else
                    {
                        // std::cout << ", e" << genElem(rdid, tinum, numThreads, maxops);
                        ++rdid;
                    }

                    --nummain;
                }

                std::cout << std::endl;
            }
        }

        std::sort(dataset.begin(), dataset.end());

        std::vector<size_t>::iterator pos = std::adjacent_find(dataset.begin(), dataset.end());

        while (pos != dataset.end())
        {
            std::cerr << "non-unique element: " << *pos << std::endl;

            pos = std::adjacent_find(std::next(pos), dataset.end());
        }
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

    /*
    template <class N, class Fn>
    bool matchOpt0(const std::vector<std::string>& args, N& pos, std::string opt, Fn fn)
    {
    std::string arg(args.at(pos));
    if (arg.find(opt) != 0) return false;
    fn();
    ++pos;
    return true;
    }
    */

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

    template <class T>
    static void setField(T &fld, T val)
    {
        fld = val;
    }

    static void help(const std::string &executable)
    {
        TestOptions tmp;

        std::cout << "A first test harness for associative containers: " << executable << std::endl
                  << "tests " << typeid(container_type).name() << std::endl
                  << std::endl
                  << "usage: " << executable << " [arguments]" << std::endl
                  << std::endl
                  << "arguments:" << std::endl
                  << "-n num   number of total operations executed (default: " << tmp.numops << ")\n"
                  << "-f name  name of the mmaped file (default: " << tmp.filename << ")\n"
                  << "-t num   number of threads (default: " << tmp.numthreads << ")\n"
                  << "-p num   number of parallel runs (default: " << tmp.numruns << ")\n"
                  << "-c num   sets initial container capacity to 2^num (default: " << tmp.capacity << ")\n"
                  << "-s       if specified, a sequential test is run before the parallel tests.\n"
                  << "-d       if specified, the generated data test is printed and tested for uniqueness.\n"
                  << "-h       displays this help message\n"
                  << std::endl;

        exit(0);
    }

    template <class T>
    static inline void unused(const T &) {}

    void prepare_test(const TestOptions &settings)
    {
        std::remove(settings.filename.c_str());
    }
};

std::atomic<bool> TestFramework::ptest_failed;
std::atomic<size_t> TestFramework::waiting_threads;

#endif