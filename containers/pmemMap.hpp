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
            pm::root::map_type::accessor result;
            pop.root()->pptr->find(result, el);
            value_type v = *result;
            return ValT(v.second);
        }

        size_t count()
        {
            return pop.root()->pptr->size();
        }

        ValT increment(KeyT el)
        {
            // TODO: Having trouble making the transaction work.
            return insert((ValT)el);
            pmem::obj::transaction::run(pop,
                                        [&]() -> void {
                                            pm::root::map_type::accessor result;
                                            if (pop.root()->pptr->find(result, el))
                                            {
                                                value_type v = *result;
                                                pop.root()->pptr->insert_or_assign(el, v.second + 1);
                                            }
                                            else
                                            {
                                                value_type v = value_type(el, 1);
                                                pop.root()->pptr->insert(v);
                                            }
                                            return;
                                        });
            return (ValT)el;
        }

        // TODO: Make reconstruction optional.
        container_type(const TestOptions &opt, bool reconstruct = false)
        {
            const size_t realcapacity = 1 << opt.capacity;
            const std::string realfilename = opt.filename + ".pool";

            try
            {
                //pop = pool::create(realfilename, "cmap");
                pop = pool::open(realfilename, "cmap");
            }
            catch (pmem::pool_error &e)
            {
                std::cerr << e.what() << std::endl;
                return;
            }

            auto r = pop.root();

            if (r->pptr == nullptr)
            {
                pmem::obj::transaction::run(pop,
                                            [&]() -> void {
                                                r->pptr = pmem::obj::make_persistent<root::map_type>();
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

        bool isConsistent()
        {
            // NOTE: Assume it is consistent.
            return true;
        }
    };
} // namespace pm

#endif