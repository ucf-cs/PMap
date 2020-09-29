#ifndef ONEFILE_MAP_HPP
#define ONEFILE_MAP_HPP

// Force persistence behavior, even if we don't detect persistent memory.
// Used for testing only.
#define PWB_IS_CLFLUSHOPT 1

#include "container.hpp"

#include "ptms/PMDKTM.hpp"
#include "ptms/OneFilePTMLF.hpp"
#include "pdatastructures/TMHashMap.hpp"

namespace onefile
{
    struct container_type : Container
    {
        // using PTM    = poflf::OneFileLF;
        // using TMTYPE = poflf::tmtype;
        using PTM = pmdk::PMDKTM;

        template <class T>
        using TMTYPE = pmdk::persist<T>;

        TMHashMap<KeyT, ValT, PTM, TMTYPE> *c;

        bool insert(ValT val)
        {
            return c->add(val);
        }

        bool erase(KeyT val)
        {
            return c->remove(val);
        }

        bool contains(KeyT el)
        {
            return c->contains(el);
        }

        ValT get(KeyT el)
        {
            ValT v;
            c->innerGet(el, v, true);
            return v;
        }

        size_t count()
        {
            return c->size();
        }

        ValT increment(KeyT el)
        {
            PTM::template updateTx<bool>([&]() {
                if (c->contains(el))
                {
                    ValT v;
                    c->innerGet(el, v, true);
                    return c->innerPut(el, v + 1, v, false);
                }
                else
                {
                    ValT val = 0;
                    return c->innerPut(el, 1, val, false);
                }
            });
        }

        // TODO: Implement recovery?
        container_type(const TestOptions &opt, bool reconstruct = false)
        {
            PTM::template updateTx<bool>([&]() {
                const size_t realcapacity = 1 << opt.capacity;
                c = PTM::template tmNew<TMHashMap<KeyT, ValT, PTM, TMTYPE>>(realcapacity);
                return true;
            });
            if (c == nullptr)
            {
                throw std::runtime_error("could not allocate");
            }
            return;
        }

        bool isConsistent()
        {
            // NOTE: Do nothing for now.
            return true;
        }
    };
} // namespace onefile

#endif