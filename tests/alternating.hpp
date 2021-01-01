#ifndef ALTERNATING_HPP
#define ALTERNATING_HPP

#include "test.hpp"

// An alternating test.
// Preinserts elements, then alternates random insertions and removals.
namespace alternatingTest
{
    struct test_type : Test
    {
        void container_test_prefix(ThreadInfo &ti)
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
                    int succ = ((container_type *)ti.container)->insert(elem);

                    assert(succ >= 0);
                    ++ti.succ;
                    ++wrid;
                    // std::cout << "insert' " << elem << " " << succ << std::endl;

                    --numops;
                }
            }
            catch (int errc)
            {
                std::cerr << "err: " << errc << std::endl;
            }
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
                // set numops to nummain (after prefix has been executed)
                while (nummain)
                {
                    if (nummain % 2)
                    {
                        int elem = genElem(wrid, tinum, ti.num_threads, maxops);
                        int succ = ((container_type *)ti.container)->insert(elem);

                        assert(succ >= 0);
                        ++wrid;
                        ++ti.succ;
                        // std::cout << "insert " << elem << " " << succ << std::endl;
                    }
                    else
                    {
                        int elem = genElem(rdid, tinum, ti.num_threads, maxops);
                        int succ = ((container_type *)ti.container)->erase(elem);

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
                std::cerr << "err: " << errc << std::endl;
            }
        }
        void container_test_suffix(ThreadInfo &ti)
        {
            return;
        }

        bool consistency_check(Test *test, const TestOptions &opt)
        {
            container_type *contptr = new container_type(opt, true);

            const size_t actsize = contptr->count();

            check_all_elements(contptr, actsize, opt.numops, opt.numthreads);

            std::cout << actsize << std::endl;
            std::cout << "Recovery check complete." << std::endl;
            return true;
        }

        size_t genElem(size_t num, size_t thrid, size_t, size_t maxOpsPerThread)
        {
            assert(num < maxOpsPerThread);

            return thrid * maxOpsPerThread + num;
        }

        std::pair<size_t, bool>
        genElemChecked(size_t num, size_t thrid, size_t maxthread, size_t maxOpsPerThread)
        {
            if (num >= maxOpsPerThread)
                return std::make_pair(size_t(), false);

            return std::make_pair(genElem(num, thrid, maxthread, maxOpsPerThread), true);
        }

        // Per-thread element validation.
        std::pair<int, bool> check_elements(const ThreadInfo &ti)
        {
            typedef std::pair<size_t, bool> checked_elem_t;

            const size_t maxops = opsPerThread(ti.num_threads, ti.pnoiter, 0);
            const size_t numops = opsPerThread(ti.num_threads, ti.pnoiter, ti.num);
            const size_t nummain = opsMainLoop(numops);
            const size_t initwr = numops - nummain;
            size_t rdid = initwr / 2;

            size_t numvalid = 0;
            bool success = true;

            // the first [0, rdid) elements must be in the data structure
            {
                for (size_t opid = 0; opid < rdid; ++opid)
                {
                    const checked_elem_t val = genElemChecked(opid, ti.num, ti.num_threads, maxops);

                    assert(val.second);
                    if (((container_type *)ti.container)->contains(val.first))
                    {
                        ++numvalid;
                        // std::cerr << val << std::endl;
                        // std::cerr << "--> " << numvalid << std::endl;
                    }
                    else
                    {
                        // std::cerr << "#\n";
                        success = false;
                    }
                }
            }

            // then find the first element that was not removed
            {
                checked_elem_t val = genElemChecked(rdid, ti.num, ti.num_threads, maxops);

                while (val.second && !((container_type *)ti.container)->contains(val.first))
                {
                    ++rdid;
                    val = genElemChecked(rdid, ti.num, ti.num_threads, maxops);
                }
                // std::cerr << "==> " << val << std::endl;
            }

            // the next X elements must be in the data structure
            {
                const size_t expsequ = initwr - (initwr / 2);
                size_t cntsequ = 0;
                checked_elem_t val = genElemChecked(rdid, ti.num, ti.num_threads, maxops);

                while (val.second && ((container_type *)ti.container)->contains(val.first))
                {
                    // std::cerr << val << std::endl;
                    ++cntsequ;
                    ++rdid;
                    val = genElemChecked(rdid, ti.num, ti.num_threads, maxops);
                }

                numvalid += cntsequ;
                // std::cerr << "->> " << numvalid << " / " << cntsequ << std::endl;

                // \todo this is an estimate and the range coult be tightened,
                //       depending if initwr+1 is an insert or remove operation
                if ((initwr > 0) && (cntsequ < expsequ - 1))
                {
                    success = false;
                    // std::cerr << "#1 " << initwr << ", " << expsequ << std::endl;
                }

                if (cntsequ > expsequ + 1)
                {
                    success = false;
                    // std::cerr << "#2\n";
                }
            }

            // no subsequent element must be in the data structure
            {
                // \todo this is an estimate and the range coult be tightened,
                //       depending if initwr+1 is an insert or remove operation
                const size_t limit = initwr + (((nummain / 2) > 0) ? (nummain / 2) - 1 : 0);
                // std::cerr << limit << " <> " << maxops << std::endl;
                assert(limit <= maxops);

                for (size_t opid = rdid; opid < limit; ++opid)
                {
                    const checked_elem_t val = genElemChecked(opid, ti.num, ti.num_threads, maxops);

                    assert(val.second);
                    if (((container_type *)ti.container)->contains(val.first))
                    {
                        success = false;
                        // std::cerr << "unexpected " << val << std::endl;
                    }
                }
            }
            return std::make_pair(numvalid, success);
        }

        // Check the number of elements contained in the data structure.
        // If any inconsistencies are found in size or in check_elements testing after recovery, something is wrong.
        void check_all_elements(container_type *cont, size_t actsize, size_t numops, size_t numthreads)
        {
            size_t contsz = 0;
            bool success = true;
            for (size_t i = 0; i < numthreads; ++i)
            {
                std::pair<int, bool> res = check_elements(ThreadInfo{cont, i, numops, numthreads});
                contsz += res.first;
                if (!res.second)
                {
                    success = false;
                    std::cerr << '!' << i << '\n';
                }
            }
            if (contsz != actsize)
            {
                std::cerr << "Unexpected size: "
                          << actsize << " <actual != found> " << contsz
                          << std::endl;
                fail();
            }
            if (!success)
            {
                std::cerr << "Inconsistent data structure"
                          << std::endl;
                fail();
            }
        }

        void fail()
        {
            throw std::runtime_error("error");
        }
    };
} // namespace alternatingTest

#endif