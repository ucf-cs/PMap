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
            ValT shiftedEl = el << ConcurrentHashMap<KeyT, ValT>::BITS_MARKED;
            assert((shiftedEl >> ConcurrentHashMap<KeyT, ValT>::BITS_MARKED) == el);

            ValT x = c->put(shiftedEl, shiftedEl);
            return x == shiftedEl;
        }

        bool erase(ValT el)
        {
            return c->remove(el << ConcurrentHashMap<KeyT, ValT>::BITS_MARKED);
        }

        bool contains(KeyT el)
        {
            return c->containsKey(el << ConcurrentHashMap<KeyT, ValT>::BITS_MARKED);
        }

        ValT get(KeyT el)
        {
            return c->get(el << ConcurrentHashMap<KeyT, ValT>::BITS_MARKED) >> ConcurrentHashMap<KeyT, ValT>::BITS_MARKED;
        }

        size_t count()
        {
            return c->size();
        }

        ValT increment(KeyT el)
        {
            return c->update(el << ConcurrentHashMap<KeyT, ValT>::BITS_MARKED, ((((size_t)1 << 61) - 3) << ConcurrentHashMap<KeyT, ValT>::BITS_MARKED), ConcurrentHashMap<KeyT, ValT>::Table::increment);
        }

        container_type(const TestOptions &opt, bool reconstruct = false)
        {
            const size_t realcapacity = 1 << opt.capacity;
            const char *path = opt.filename.c_str();
            c = new ConcurrentHashMap<KeyT, ValT>(path, realcapacity, reconstruct);
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