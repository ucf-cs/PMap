#ifndef DEGREE_HPP
#define DEGREE_HPP

#include "test.hpp"

// Degree counting test.
// Takes pairs and counts outbound connections.
// Intended to accept RMAT files.
// In structures that cannot count, simply records existance of an outbound connection of a node.
namespace degreeTest
{
    struct test_type : Test
    {
    private:
        // TODO: Adapt this.
        static void reportDegree(ThreadInfo &ti)
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
                ((container_type *)ti.container)->increment(incoming);
            }
        }

    public:
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
                std::cerr << "err: " << errc << std::endl;
            }
        }
        void container_test_suffix(ThreadInfo &ti)
        {
            reportDegree(ti);
            return;
        }
    };
} // namespace degreeTest

#endif