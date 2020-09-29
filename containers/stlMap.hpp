#ifndef STL_MAP_HPP
#define STL_MAP_HPP

#include "container.hpp"

#include <map>
#include <mutex>

namespace stl
{

    struct container_type : Container
    {
        using mutex_type = std::mutex;
        using guard_type = std::lock_guard<mutex_type>;

        std::map<KeyT, ValT> *c;
        mutex_type global_lock;

        bool insert(ValT el)
        {
            using map_element = std::map<KeyT, ValT>::value_type;
            guard_type g(global_lock);
            return c->insert(map_element(el, el)).second;
        }

        bool erase(KeyT el)
        {
            guard_type g(global_lock);
            return c->erase(el) == 1;
        }

        bool contains(KeyT el)
        {
            guard_type g(global_lock);
            return c->count(el) > 0;
        }

        ValT get(KeyT el)
        {
            try
            {
                return c->at(el);
            }
            catch (const std::out_of_range &oor)
            {
                return (ValT)NULL;
            }
        }

        size_t count()
        {
            return c->size();
        }

        ValT increment(KeyT el)
        {
            guard_type g(global_lock);
            // Get the value.
            ValT t = (*c)[el];
            // Increment by 1.
            t++;
            // Store that value.
            (*c)[el] = t;
            return t;
        }

        // This constructor offers no persistence.
        // Reconstruct is unused.
        container_type(const TestOptions &, __attribute__((unused)) bool reconstruct = false)
        {
            c = new std::map<KeyT, ValT>();
            if (c == nullptr)
            {
                throw std::runtime_error("could not allocate");
            }
            return;
        }

        bool isConsistent()
        {
            // This container offers no persistence.
            // Thus, a "recovered" container is always empty.
            return true;
        }
    };
} // namespace stl

#endif