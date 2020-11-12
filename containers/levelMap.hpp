#ifndef CLEVEL_MAP_HPP
#define CLEVEL_MAP_HPP

#include <libpmemobj++/make_persistent.hpp>
#include <libpmemobj++/p.hpp>
#include <libpmemobj++/transaction.hpp>
#include <libpmemobj++/persistent_ptr.hpp>
#include <libpmemobj++/pool.hpp>
#include <libpmemobj++/allocator.hpp>

#include <libpmemobj++/experimental/clevel_hash.hpp>

#include <cassert>

#include <unistd.h>

#include "container.hpp"

namespace clevel
{
    static inline int
    file_exists(char const *file)
    {
        return access(file, F_OK);
    }

    struct root
    {
        using map_type = pmem::obj::experimental::clevel_hash<pmem::obj::p<KeyT>, pmem::obj::p<ValT>>;

        pmem::obj::persistent_ptr<map_type> cons;
    };

    struct container_type : Container
    {
        using pool = pmem::obj::pool<root>;
        using value_type = root::map_type::value_type;
        using key_type = root::map_type::key_type;

        pool pop;

        bool insert(ValT val)
        {
            auto map = pop.root()->cons;
            assert(map != nullptr);

            // NOTE: id appears unused. Just pass in the key.
            auto r = map->insert(value_type(val, val), localThreadNum, static_cast<size_t>(val));
            return (!r.found);
        }

        bool erase(KeyT key)
        {
            auto map = pop.root()->cons;
            assert(map != nullptr);

            auto r = map->erase(key_type(key), 1);
            return (r.found);
        }

        bool contains(KeyT key)
        {
            auto map = pop.root()->cons;
            assert(map != nullptr);

            auto r = map->search(key_type(key));
            return (r.found);
        }

        ValT get(KeyT key)
        {
            // TODO: No accessor in clevel_hash seemingly means no support for get().
            // Fall back to contains.

            auto map = pop.root()->cons;
            assert(map != nullptr);

            auto r = map->search(key_type(key));
            return r.found ? key : 0;
        }

        size_t count()
        {
            // TODO: This is seemingly untracked.
            // Use capacity instead.
            auto map = pop.root()->cons;
            assert(map != nullptr);

            return map->capacity();
        }

        ValT increment(KeyT el)
        {
            // TODO: Seemingly unsupported.
            return insert((ValT)el);
        }

        container_type(const TestOptions &opt, bool reconstruct = false)
        {
            const size_t realcapacity = 1 << opt.capacity;
            const std::string realfilename = opt.filename + ".pool";
            const char *path = realfilename.c_str();

            if (!reconstruct && access(path, F_OK) != 0)
            {
                pop = pmem::obj::pool<root>::create(path, "clevel_hash",
                                                    PMEMOBJ_MIN_POOL * 20,
                                                    S_IWUSR | S_IRUSR);
                auto proot = pop.root();

                pmem::obj::transaction::manual tx(pop);

                proot->cons = pmem::obj::make_persistent<root::map_type>();
                proot->cons->set_thread_num(1);

                pmem::obj::transaction::commit();
            }
            else
            {
                pop = pmem::obj::pool<root>::open(realfilename, "clevel_hash");
            }
            return;
        }

        bool isConsistent()
        {
            // NOTE: Assume it is consistent.
            return true;
        }
    };
} // namespace clevel

#endif