#include <thread>
#include <iostream>
#include <sstream>
#include <list>
#include <iomanip>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <cassert>
#include <atomic>
#include <vector>
#include <algorithm>
#include <new>

#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#ifndef DEFAULT_CACHELINE_SIZE
static constexpr size_t CACHELINESZ = std::hardware_destructive_interference_size;
#else
static constexpr size_t CACHELINESZ = DEFAULT_CACHELINE_SIZE;
#endif

#ifndef TEST_CONFLICTS
#define TEST_CONFLICTS 0
#endif

static constexpr bool KILL_HARD = true;

struct TestOptions
{
  size_t      numthreads     = 8;
  size_t      numops         = 40;
  size_t      numruns        = 1;
  size_t      capacity       = 16; // actual capacity is 2 ^ capacity
  bool        sequential     = false;
  bool        printdata      = false;
	int         fail_timer     = 0;
	bool        test_recovery  = false;
  std::string filename       = "./chashmap.dat";
};

using KeyT = size_t;
using ValT = size_t;


#include "test-ucfMap.hpp"
#include "test-stlMap.hpp"
#include "test-pmemMap.hpp"
#include "test-onefileMap.hpp"

//
// define container and operations
// i.e., insert, erase, count

using container_type = ucf::container_type; 
// using container_type = stl::container_type; 
// using container_type = onefile::container_type; 
// using container_type = pm::container_type; 


// end define container

namespace
{

template <class T>
inline
void unused(const T&) {}


struct alignas(CACHELINESZ) ThreadInfo
{
  container_type* container;
  size_t          num;
  size_t          fail;
  size_t          succ;
  size_t          pnoiter;
  size_t          num_threads;
  size_t          num_held_back;
  // unsigned        cpunode_start;
  // unsigned        cpunode_end;
  // std::set        elemsin;
  // std::set        elemsout;

  ThreadInfo(container_type* r, size_t n, size_t cntiter, size_t cntthreads)
  : container(r), num(n), fail(0), succ(0), pnoiter(cntiter), num_threads(cntthreads),
    num_held_back(0)
    // , cpunode_start(0), cpunode_end(0)
  {
    assert(num < num_threads);
  }

  ThreadInfo()
  : ThreadInfo(nullptr, 0, 0, 1)
  {}
};

void fail()
{
  throw std::runtime_error("error");
}

/// counts number of threads that are ready to run
std::atomic<size_t> waiting_threads;

/// waits until all threads have been created and are ready to run
void sync_start()
{
  assert(waiting_threads.load());
  waiting_threads.fetch_add(-1);

  while (waiting_threads.load()) {} 
}

/// computes number of operations that a thread will carry out
size_t opsPerThread(size_t maxthread, size_t totalops, size_t thrid)
{
  assert(maxthread > thrid);

  size_t numops = totalops / maxthread;
  size_t remops = totalops % maxthread;

  if (remops > 0 && thrid < remops) ++numops;

  return numops;
}

/// computes number of operations in the main loop; the remainder of
///   operations will be executed prior to the main loop (i.e., insert elements)
size_t opsMainLoop(size_t numops)
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
    int numops  = opsPerThread(maxthread, totalops, i);
    int nummain = opsMainLoop(numops);

		// std::cerr << numops << "<o m>" << nummain << std::endl;

    // special case when nummain is even and small
    //   then the insert happens after the delete
    if ((numops - nummain == 0) && (nummain % 2 == 0))
      elems += numops / 2;
    else
      elems += (numops - nummain);

    // if odd, we insert one more
    if (nummain % 2) ++elems;
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

size_t genElem(size_t num, size_t thrid, size_t, size_t maxOpsPerThread)
{
  assert(num < maxOpsPerThread);

  return thrid * maxOpsPerThread + num;
}

#endif


std::atomic<bool> ptest_failed(false);

void container_test_prefix(ThreadInfo& ti)
{
  const size_t tinum     = ti.num;
  const size_t maxops    = opsPerThread(ti.num_threads, ti.pnoiter, 0);
  size_t       numops    = opsPerThread(ti.num_threads, ti.pnoiter, tinum);
  const size_t nummain   = opsMainLoop(numops);
  size_t       wrid      = 0;

  try
  {
    while (numops > nummain)
    {
      int     elem = genElem(wrid, tinum, ti.num_threads, maxops);
      int     succ = insert(*ti.container, elem);

      assert(succ >= 0), unused(succ);
      ++ti.succ;
			++wrid;
      // std::cout << "insert' " << elem << " " << succ << std::endl;

      --numops;
    }
  }
  catch (int errc)
  {
    ptest_failed = true;
    std::cerr << "err: " << errc << std::endl;
  }
}


void container_test(ThreadInfo& ti)
{
  const size_t tinum   = ti.num;
  const size_t maxops  = opsPerThread(ti.num_threads, ti.pnoiter, 0);
  const size_t numops  = opsPerThread(ti.num_threads, ti.pnoiter, tinum);
  size_t       nummain = opsMainLoop(numops);
  size_t       wrid    = numops - nummain;
  size_t       rdid    = wrid / 2;

  try
  {
    // set numops to nummain (after prefix has been executed)
    assert(nummain > 0);
    while (nummain)
    {
      if (nummain % 2)
      {
        int    elem = genElem(wrid, tinum, ti.num_threads, maxops);
        int    succ = insert(*ti.container, elem);

        assert(succ >= 0), unused(succ);
        ++wrid;
        ++ti.succ;
	      // std::cout << "insert " << elem << " " << succ << std::endl;
      }
      else
      {
        int    elem = genElem(rdid, tinum, ti.num_threads, maxops);
        int    succ = erase(*ti.container, elem);
        
	      ++rdid;
	      if (succ > 0) ++ti.succ; else ++ti.fail;
	      // std::cout << "erase " << elem << " " << succ << std::endl;
      }

      --nummain;
    }

    // ti.container->getAllocator().release_memory();
    // ti.num_held_back = ti.container->getAllocator().has_unreleased_memory();
  }
  catch (int errc)
  {
    ptest_failed = true;

    std::cerr << "err: " << errc << std::endl;
  }
}

using duration_unit = std::chrono::milliseconds;
using time_point    = std::chrono::time_point<std::chrono::system_clock>;

void ptest(ThreadInfo& ti, time_point& starttime)
{
  container_test_prefix(ti);
  sync_start();

  if (ti.num == 0) starttime = std::chrono::system_clock::now();
  
  container_test(ti);
}


[[noreturn]]
void simulate_catastrophic_failure()
{
  if (KILL_HARD) kill(getpid(), SIGKILL);

	std::abort();
}

void timed_catastrophe(int delay)
{
  while (waiting_threads.load()) {}

  std::this_thread::sleep_for(std::chrono::seconds(delay));

  simulate_catastrophic_failure();
}

std::pair<int, bool> 
check_elements(const ThreadInfo& ti)
{
  const size_t maxops   = opsPerThread(ti.num_threads, ti.pnoiter, 0);
  const size_t numops   = opsPerThread(ti.num_threads, ti.pnoiter, ti.num);
  const size_t nummain  = opsMainLoop(numops);
	const size_t initwr   = numops - nummain;
	size_t       rdid     = initwr / 2;

	size_t       numvalid = 0;
	bool         success  = true;
	
	// the first [0, rdid) elements must be in the data structure
	{
	  for (size_t opid = 0; opid < rdid; ++opid)
  	{
      const int val = genElem(opid, ti.num, ti.num_threads, maxops);

		  if (contains(*ti.container, val))
		    ++numvalid;
		  else
		    success = false;
	  }
	}
	
	// then find the first element that was not removed
	{
	  int val = genElem(rdid, ti.num, ti.num_threads, maxops);

	  while (!contains(*ti.container, val))
		{
		  ++rdid;
			val = genElem(rdid, ti.num, ti.num_threads, maxops);
		}
	}
	
	// the next X elements must be in the data structure
	{
	  size_t cntsequ = 0;
		int    val = genElem(rdid, ti.num, ti.num_threads, maxops);

		while (contains(*ti.container, val))
		{
		  ++cntsequ;
			++rdid;
			val = genElem(rdid, ti.num, ti.num_threads, maxops);
		}

		numvalid += cntsequ;

    // \todo this is an estimate and the range coult be tightened, 
		//       depending if initwr+1 is an insert or remove operation
		if ((initwr > 0) && (cntsequ < initwr - 1)) success = false;
		if (cntsequ > initwr + 1) success = false;
	}
	
	// no subsequent element must be in the data structure
	{
    // \todo this is an estimate and the range coult be tightened, 
		//       depending if initwr+1 is an insert or remove operation
	  const size_t limit = initwr + (nummain / 2) - 1;

	  for (size_t opid = rdid; opid < limit; ++opid)
		{
		  const int val = genElem(opid, ti.num, ti.num_threads, maxops);

			if (contains(*ti.container, val)) success = false;
		}
	}

  return std::make_pair(numvalid, success);
}

void recovery_test(const TestOptions& opt)
{
  container_type* const           tag = nullptr;
	std::unique_ptr<container_type> cont{&reconstruct(opt, tag)};
	int                             contsz = 0;
	bool                            success = true;

  for (size_t i = 1; i < opt.numthreads; ++i)
	{
	  std::pair<int, bool> res = check_elements(ThreadInfo{cont.get(), i, opt.numops, opt.numthreads});

		contsz += res.first;
		if (!res.second) success = false;
  }

  const int actsize = count(*cont);

  if (contsz != actsize)
  {
	  std::cerr << "Unexpected size: "
		          << actsize << " <actual != found> " << contsz
		          << std::endl;
		fail();
  }

	if (!success)
	{
	  std::cerr << "Recovery inconsistency" 
		          << std::endl;
		fail();
	}

  std::cout << actsize << std::endl;
	std::cout << "check OK." << std::endl;
}

size_t sequential_test(const TestOptions& opt)
{
  container_type* const           tag  = nullptr;
  std::unique_ptr<container_type> cont{&construct(opt, tag)};
  ThreadInfo                      info{cont.get(), 0, opt.numops, 1};

  std::cout << std::endl;
  container_test_prefix(info);
  
  time_point starttime = std::chrono::system_clock::now();

  container_test(info);

  time_point endtime = std::chrono::system_clock::now();
  const int  elapsedtime = std::chrono::duration_cast<duration_unit>(endtime-starttime).count();
  const int  actsize = count(*cont);
  const int  expctsize = expectedSize(1, opt.numops);
  
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


int parallel_test(const TestOptions& opt)
{
  std::cout << std::endl;

  std::list<std::thread>          exp_threads;
  std::vector<ThreadInfo>         thread_info{opt.numthreads, ThreadInfo{}};
  container_type* const           tag  = nullptr;
  std::unique_ptr<container_type> cont{&construct(opt, tag)}; // for lifetime management
  container_type*                 contptr = cont.get();
  time_point                      starttime;

  waiting_threads = opt.numthreads;

  // spawn
  for (size_t i = 1; i < opt.numthreads; ++i)
  {
    ThreadInfo& ti = thread_info.at(i);

    ti = ThreadInfo(contptr, i, opt.numops, opt.numthreads);
    exp_threads.emplace_back(ptest, std::ref(ti), std::ref(starttime));
  }

	if (opt.fail_timer)
	{
	  std::thread catastrophe(timed_catastrophe, opt.fail_timer);

		catastrophe.detach();
	}

  // use main thread as worker
  ThreadInfo& thisTi = thread_info.front();

  thisTi = ThreadInfo(contptr, 0, opt.numops, opt.numthreads);
  ptest(thisTi, starttime); 

  // join
  for (std::thread& thr : exp_threads) thr.join();

  time_point     endtime = std::chrono::system_clock::now();
  int            elapsedtime = std::chrono::duration_cast<duration_unit>(endtime-starttime).count();

  // __sync_synchronize();
  const int actsize = count(*cont); // std::distance(cont.qbegin(), cont.qend());
  std::cout << "elapsed time = " << elapsedtime << "ms" << std::endl;
  std::cout << "container size = " << actsize << std::endl;

  std::cerr << elapsedtime << std::endl;

  size_t total_succ = 0;
  size_t total_fail = 0;

  for (ThreadInfo& info : thread_info)
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
	const size_t        maxops = opsPerThread(numThreads, nops, 0);

  for (size_t tinum = 0; tinum < numThreads; ++tinum)
	{
    {
      size_t       numops  = opsPerThread(numThreads, nops, tinum);
		  const size_t nummain = opsMainLoop(numops);
		  size_t       wrid = 0;

	    std::cout << tinum << ": " << numops << "/" << nummain << std::endl;
		  while (numops > nummain)
      {
		    dataset.push_back(genElem(wrid, tinum, numThreads, maxops));
			  ++wrid; --numops;
		  // std::cout << ", i" << dataset.back();
		  }
    }

    {
      std::cout << " - ";

      const size_t numops  = opsPerThread(numThreads, nops, tinum);
      size_t       nummain = opsMainLoop(numops);
      size_t       wrid    = numops - nummain;
      size_t       rdid    = wrid / 2;

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
U conv(const V& val)
{
  std::stringstream tmp;
  U                 res;

  tmp << val;
  tmp >> res;
  return res;
}

template <class N, class T>
bool matchOpt1(const std::vector<std::string>& args, N& pos, std::string opt, T& fld)
{
  std::string arg(args.at(pos));

  if (arg.find(opt) != 0) return false;

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
bool matchOpt0(const std::vector<std::string>& args, N& pos, std::string opt, Fn fn, Parms... parms)
{
  std::string arg(args.at(pos));

  if (arg.find(opt) != 0) return false;

  fn(parms...);
  ++pos;
  return true;
}

template <class T>
void setField(T& fld, T val)
{
  fld = val;
}
	

void help(const std::string& executable)
{
  TestOptions tmp;

  std::cout << "A first test harness for associative containers: " << executable << std::endl
            << "  tests " << typeid(container_type).name() << '\n'
					  << "\nUsage: " << executable << " [arguments]\n" 
					  << "\nArguments\n"
						<< "Operation description:\n"
					  << "  -n num   number of total operations executed (default: " << tmp.numops << ")\n"
					  << "  -t num   number of threads (default: " << tmp.numthreads << ")\n"
					  << "  -p num   number of parallel runs (default: " << tmp.numruns << ")\n"
					  << "  -c num   sets initial container capacity to 2^num (default: " << tmp.capacity << ")\n"
						<< "File Mapping:"
					  << "  -f name  name of the mmaped file (default: " << tmp.filename << ")\n"
						<< "Failure Testing:\n"
						<< "  -F num   generates a catastrophic failure after num seconds to terminate the program.\n"
						<< "           (default: " << tmp.fail_timer << ")\n"
						<< "  -R       starts recovery mode from catastrophic failure.\n"
						<< "Debugging"
					  << "  -s       if specified, a sequential test is run before the parallel tests.\n"
					  << "  -d       if specified, the generated data test is printed and tested for uniqueness.\n"
						<< "General Information"
					  << "  -h       displays this help message\n"
					  << std::endl;

  exit(0);
}

void prepare_test(const TestOptions& settings)
{
  std::remove(settings.filename.c_str()); 
}

} // anonymous namespace




int main(int argc, char** args)
{
  std::vector<std::string>   arguments(args, args+argc);
  size_t                     argn = 1;
  size_t                     seqtime = 0;
	bool                       matched = true;
  TestOptions                settings;

  while (matched && (argn < arguments.size()))
  {
    matched = (  matchOpt1(arguments, argn, "-n", settings.numops)
              || matchOpt1(arguments, argn, "-f", settings.filename)
              || matchOpt1(arguments, argn, "-t", settings.numthreads)
              || matchOpt1(arguments, argn, "-p", settings.numruns)
              || matchOpt1(arguments, argn, "-c", settings.capacity) 
              || matchOpt0(arguments, argn, "-s", setField<bool>, std::ref(settings.sequential), true)
              || matchOpt0(arguments, argn, "-d", setField<bool>, std::ref(settings.printdata), true)
              || matchOpt0(arguments, argn, "-h", help, arguments.at(0))
              || matchOpt1(arguments, argn, "-F", settings.fail_timer) 
              || matchOpt0(arguments, argn, "-R", setField<bool>, std::ref(settings.test_recovery), true)
							);
  }

  if (argn != arguments.size())
  {
    std::cerr << "unknown argument: " << arguments.at(argn) << std::endl;
    exit(1);
  }

  std::cout << "*** concurrent container test " 
	          << "\n*** total number of operations: " << settings.numops 
						<< "\n***          number of threads: " << settings.numthreads 
            << "\n***                mapped file: " << settings.filename 
            << "\n***      initial capacity base: " << settings.capacity 
						   << " ( " << (1<<settings.capacity) << ")" 
            << "\n***             container type: " << typeid(container_type).name() 
						<< std::endl;

  if (settings.printdata)
    prnGen(settings.numthreads, settings.numops);


	if (settings.test_recovery)
	{
	  try
		{
		  recovery_test(settings);
		}
		catch (...)
		{
		  std::cout << "error in data recovery..." << std::endl;
		}
	}

  if (settings.sequential)
  {
    try
    { 
      std::cout << "\n***** sequential test" << std::endl;
			prepare_test(settings);
      seqtime = sequential_test(settings);
    }
    catch (...)
    {
      std::cout << "error in sequential test..." << std::endl;
    }
  }

  try
  {
    size_t total_time = 0;

    for (size_t i = 1; i <= settings.numruns; ++i)
    {
      std::cout << "\n***** parallel test: " << i << std::endl;
      prepare_test(settings);
			total_time += parallel_test(settings);
    }

    if (settings.numruns)
    {
      std::cout << "average time: " << (total_time/settings.numruns) << std::endl;
    }
    
    std::cout << std::endl;
  }
  catch (const std::runtime_error& err)
  {
    std::cout << "error in parallel test: " << err.what() << std::endl;
  }
  catch (...)
  {
    std::cout << "error in parallel test..." << std::endl;
  }

  unused(seqtime);
  return 0;
}
