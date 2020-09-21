#ifndef _TESTUCFMAP
#define _TESTUCFMAP 1

#include "hashMap.hpp"

namespace ucf
{
struct container_type : ConcurrentHashMap<KeyT, ValT>
{
  using base = ConcurrentHashMap<KeyT, ValT>;
  using base::base;
};

bool insert(container_type& c, ValT el)
{
  ValT shiftedEl = el << 3;
  assert(shiftedEl >> 3 == el);

  // std::cout << "i " << shiftedEl << std::endl;

  ValT x = c.put(shiftedEl, shiftedEl);
  return x == shiftedEl;
}

bool erase(container_type& c, ValT el)
{
  // std::cout << "e " << (el << 3) << std::endl;

  return c.remove(el << 3);
}

size_t count(container_type& c)
{
  return c.size();
}

bool contains(container_type& c, ValT el)
{
  return c.containsKey(el << 3);
}

container_type&
construct(const TestOptions& opt, container_type*)
{
  const size_t realcapacity = 1 << opt.capacity;
  const char*  fn = opt.filename.c_str();

  container_type* cont = new container_type(fn, realcapacity);

  if (cont == nullptr) throw std::runtime_error("could not allocate");

  return *cont;
}

container_type&
reconstruct(const TestOptions&, const container_type*)
{
  throw std::logic_error("recovery not implemented");
}

}
#endif /* _TESTUCFMAP */
