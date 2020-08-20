#ifndef TEST_UNIQUE_ELEMS
#define TEST_UNIQUE_ELEMS

#include "test-framework.hpp"

class UniqueElems : public TestFramework
{
public:
  UniqueElems()
  {
  }

private:
  static void container_test_prefix(ThreadInfo &ti)
  {
    const size_t tinum = ti.num;
    const size_t maxops = opsPerThread(ti.num_threads, ti.pnoiter, 0);
    size_t numops = opsPerThread(ti.num_threads, ti.pnoiter, tinum);
    const size_t nummain = opsMainLoop(numops);
    size_t wrid = 0;

    try
    {
      while (numops > nummain)
      {
        int elem = genElem(wrid, tinum, ti.num_threads, maxops);
        int succ = insert(*ti.container, elem);

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

  static void container_test(ThreadInfo &ti)
  {
    const size_t tinum = ti.num;
    const size_t maxops = opsPerThread(ti.num_threads, ti.pnoiter, 0);
    const size_t numops = opsPerThread(ti.num_threads, ti.pnoiter, tinum);
    size_t nummain = opsMainLoop(numops);
    size_t wrid = numops - nummain;
    size_t rdid = wrid / 2;

    try
    {
      // set numops to nummain (after prefix has been executed)
      assert(nummain > 0);
      while (nummain)
      {
        if (nummain % 2)
        {
          int elem = genElem(wrid, tinum, ti.num_threads, maxops);
          int succ = insert(*ti.container, elem);

          assert(succ >= 0), unused(succ);
          ++wrid;
          ++ti.succ;
          // std::cout << "insert " << elem << " " << succ << std::endl;
        }
        else
        {
          int elem = genElem(rdid, tinum, ti.num_threads, maxops);
          int succ = erase(*ti.container, elem);

          ++rdid;
          if (succ > 0)
            ++ti.succ;
          else
            ++ti.fail;
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
  static void ptest(ThreadInfo &ti, time_point &starttime)
  {
    container_test_prefix(ti);
    sync_start();

    if (ti.num == 0)
      starttime = std::chrono::system_clock::now();

    container_test(ti);
  }
};

#endif