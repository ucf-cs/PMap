#ifndef _TESTPMEMMAP
#define _TESTPMEMMAP 1

#include <libpmemobj++/p.hpp>
#include <libpmemobj++/persistent_ptr.hpp>
#include <libpmemobj++/pool.hpp>
#include <libpmemobj++/container/concurrent_hash_map.hpp>

namespace pm
{

// cmp. w/ example in doxygen docu for pmem's concurrent_hash_map

struct root 
{
  using map_type = pmem::obj::concurrent_hash_map<pmem::obj::p<KeyT>, pmem::obj::p<ValT>>;

  pmem::obj::persistent_ptr<map_type> pptr;
};

struct container_type 
{
  using pool       = pmem::obj::pool<root>;
  using value_type = root::map_type::value_type;

  container_type(const std::string& s, size_t initialcapacity)
  : pop{ pool::open(s, "cmap") }
  {
    auto r = pop.root();
    
    if (r->pptr == nullptr)
    {
      pmem::obj::transaction::run( pop, 
                                   [&] () -> void
                                   { r->pptr = pmem::obj::make_persistent<root::map_type>(initialcapacity); 
                                   }
                                 );
    }
    else
    {
      pop.root()->pptr->runtime_initialize();
    }  
  }
 
  pool pop; 
};


bool insert(container_type& c, ValT val)
{
  using value_type = container_type::value_type;

  return c.pop.root()->pptr->insert(value_type(val, val));
}

bool erase(container_type& c, KeyT val)
{
  return c.pop.root()->pptr->erase(val);
}

size_t count(container_type& c)
{
  return c.pop.root()->pptr->size();
}

size_t contains(container_type& c, KeyT val)
{
  return c.pop.root()->pptr->count(val) > 0;
}

container_type&
construct(const TestOptions& opt, const container_type*)
{
  const size_t      realcapacity = 1 << opt.capacity;
  const std::string realfilename = opt.filename + ".pool";   
  container_type*   cont = new container_type(realfilename, realcapacity);

  if ((cont == nullptr) || (cont->pop.root()->pptr == nullptr)) 
    throw std::runtime_error("could not allocate");

  return *cont;
}

container_type&
reconstruct(const TestOptions& opt, const container_type*)
{
  throw std::logic_error("recovery not implemented");
}


}
#endif /* _TESTPMEMMAP */
