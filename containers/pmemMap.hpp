#ifndef PMEM_MAP_HPP
#define PMEM_MAP_HPP

#include "container.hpp"

#include <libpmemobj++/p.hpp>
#include <libpmemobj++/transaction.hpp>
#include <libpmemobj++/pool.hpp>
#include <libpmemobj++/allocator.hpp>

#include <libpmemobj++/persistent_ptr.hpp>
#include <libpmemobj++/container/concurrent_hash_map.hpp>

namespace pm
{
    // cmp. w/ example in doxygen docu for pmem's concurrent_hash_map

    struct root
    {
        using map_type = pmem::obj::concurrent_hash_map<pmem::obj::p<KeyT>, pmem::obj::p<ValT>>;

        pmem::obj::persistent_ptr<map_type> pptr;
    };

    struct container_type : Container
    {
        using pool = pmem::obj::pool<root>;
        using value_type = root::map_type::value_type;

        pool pop;

        bool insert(ValT val)
        {
            return pop.root()->pptr->insert(value_type(val, val));
        }

        bool erase(KeyT val)
        {
            return pop.root()->pptr->erase(val);
        }

        bool contains(KeyT el)
        {
            pm::root::map_type::accessor result;
            return pop.root()->pptr->find(result, el);
        }

        ValT get(KeyT el)
        {
            return contains(el);
        }

        size_t count()
        {
            return pop.root()->pptr->size();
        }

        ValT increment(KeyT el)
        {
            // NOTE: Cannot actually increment.
            return insert(el);
        }

        container_type(const TestOptions &opt)
        {
            const size_t realcapacity = 1 << opt.capacity;
            const std::string realfilename = opt.filename + ".pool";

            pop = pool::open(realfilename, "cmap");
            auto r = pop.root();

            if (r->pptr == nullptr)
            {
                pmem::obj::transaction::run(pop,
                                            [&]() -> void {
                                                r->pptr = pmem::obj::make_persistent<root::map_type>(realcapacity);
                                            });
            }
            else
            {
                pop.root()->pptr->runtime_initialize();
            }

            if (pop.root()->pptr == nullptr)
                throw std::runtime_error("could not allocate");

            return;
        }
    };
} // namespace pm

#endif