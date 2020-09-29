#ifndef UCF_MAP_HPP
#define UCF_MAP_HPP

#include "container.hpp"
#include "cliffMap/hashMap.hpp"

namespace ucf
{
    struct container_type : Container
    {
        ConcurrentHashMap<KeyT, ValT> *c;

        bool insert(ValT el)
        {
            ValT shiftedEl = el << 3;
            assert(shiftedEl >> 3 == el);

            ValT x = c->put(shiftedEl, shiftedEl);
            return x == shiftedEl;
        }

        bool erase(ValT el)
        {
            return c->remove(el << 3);
        }

        bool contains(KeyT el)
        {
            return c->containsKey(el << 3);
        }

        ValT get(KeyT el)
        {
            return c->get(el << 3) >> 3;
        }

        size_t count()
        {
            return c->size();
        }

        ValT increment(KeyT el)
        {
            return c->update(el << 3, ((((size_t)1 << 61) - 3) << 3), ConcurrentHashMap<KeyT, ValT>::Table::increment);
        }

        container_type(const TestOptions &opt, bool reconstruct = false)
        {
            const size_t realcapacity = 1 << opt.capacity;
            const char *fn = opt.filename.c_str();
            c = new ConcurrentHashMap<KeyT, ValT>(fn, realcapacity, reconstruct);
            if (c == nullptr)
                throw std::runtime_error("could not allocate");
            return;
        }

        bool isConsistent()
        {
            // Consistency is already checked during recovery.
            // Thus, we leave this empty for now.
            return true;
        }
    };

} // namespace ucf

#endif