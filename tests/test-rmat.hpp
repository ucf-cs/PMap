#ifndef __RMATTEST__
#define __RMATTEST__

// The cardinality of the graph.
// Needs 2^29 nodes upper bound for 4GB reddit comments.
//inline const size_t NODE_COUNT = 2048;
//inline const size_t RMAT_COUNT = 4;

#include "test-framework.hpp"

class RMAT : public TestFramework
{
public:
  RMAT()
  {
  }

private:
  // TODO: Adapt this.
  static void reportDegree(ThreadInfo &ti)
  {
    size_t elemCount = count(*ti.container);
    for (size_t i = 0; i < elemCount; i++)
    {
      if (contains(*ti.container, i << 3))
      {
        std::cout << "Node  " << i << ":\t" << (get(*ti.container, i << 3) >> 3) << std::endl;
      }
    }
  }
  inline static void parseFile(ThreadInfo &ti, std::string filename)
  {
    // Extra threads do nothing since we have only 4 files.
    if (ti.num >= 4)
    {
      return;
    }
    std::ifstream rmat(filename);
    if (!rmat.is_open())
    {
      throw std::runtime_error("Could not open file.");
    }
    std::string line;
    KeyT outgoing;
    KeyT incoming;
    size_t val;
    while (std::getline(rmat, line))
    {
      std::stringstream ss(line);
      size_t colIdx = 0;
      while (ss >> val)
      {
        switch (colIdx)
        {
        case 0:
          outgoing = val;
          break;
        case 1:
          incoming = val;
          break;
        default:
          throw std::runtime_error("Too many values found on this line.");
          break;
        }
        if (ss.peek() == ' ')
          ss.ignore();
        colIdx++;
      }
      increment(*ti.container, incoming << 3);
    }
  }
  void container_test_prefix(ThreadInfo &ti)
  {
    return;
  }

  void container_test(ThreadInfo &ti)
  {
    const size_t tinum = ti.num;
    const size_t maxops = opsPerThread(ti.num_threads, ti.pnoiter, 0);
    const size_t numops = opsPerThread(ti.num_threads, ti.pnoiter, tinum);
    size_t nummain = opsMainLoop(numops);
    size_t wrid = numops - nummain;
    size_t rdid = wrid / 2;

    try
    {
      const size_t tinum = ti.num;
      std::string str = "/home/marioman/PMap/data/rmat/edge_list_rmat_s10_" + std::to_string(tinum) + "_of_4";
      //std::string str = "./data/rmat/smallTest";
      parseFile(ti, str);

      ++wrid;
      ++ti.succ;
    }
    catch (int errc)
    {
      ptest_failed = true;

      std::cerr << "err: " << errc << std::endl;
    }
  }
  void ptest(ThreadInfo &ti, time_point &starttime)
  {
    container_test_prefix(ti);
    sync_start();

    if (ti.num == 0)
      starttime = std::chrono::system_clock::now();

    container_test(ti);
  }
};

#endif