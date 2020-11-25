#ifndef REDDIT_HPP
#define REDDIT_HPP

#include "test.hpp"

// Reddit test.
// Inputs a long list of numbers.
// Counts the number of times each number is seen.
// In structures that cannot count, simply records existance of the number.
namespace redditTest
{
    // TODO: Adapt this test for reddit input.
    struct test_type : Test
    {
    private:
        static void reportReddit(ThreadInfo &ti)
        {
            size_t elemCount = ((container_type *)ti.container)->count();
            for (size_t i = 0; i < elemCount; i++)
            {
                if (((container_type *)ti.container)->contains(i))
                {
                    std::cout << "Node  " << i << ":\t" << ((container_type *)ti.container)->get(i) << std::endl;
                }
            }
        }
        inline static void parseFile(ThreadInfo &ti, std::string filename)
        {
            // Extra threads do nothing since we have only 1 file.
            if (ti.num >= 1)
            {
                return;
            }
            std::ifstream reddit(filename);
            if (!reddit.is_open())
            {
                throw std::runtime_error("Could not open file.");
            }
            std::string line;
            uintptr_t val;
            while (std::getline(reddit, line))
            {
                std::stringstream ss(line);
                ss >> val;
                ((container_type *)ti.container)->increment(val);
            }
        }

    public:
        void container_test_prefix(__attribute__((unused)) ThreadInfo &ti)
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
                std::string str = "/home/kenneth/PMap/data/reddit_author_hash.uint64_t";
                parseFile(ti, str);

                ++wrid;
                ++ti.succ;
            }
            catch (int errc)
            {
                std::cerr << "err: " << errc << std::endl;
            }
        }
        void container_test_suffix(ThreadInfo &ti)
        {
            reportReddit(ti);
            return;
        }
    };
} // namespace redditTest

#endif