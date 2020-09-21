#ifndef _TESTSTLMAP
#define _TESTSTLMAP 1

#include <map>
#include <mutex>


namespace stl
{

struct container_type : std::map<KeyT, ValT>
{
  using base = std::map<KeyT, ValT>;
  using base::base;
};


using mutex_type     = std::mutex;
using guard_type     = std::lock_guard<mutex_type>;

mutex_type global_lock;

bool insert(container_type& c, ValT el)
{
  using map_element = container_type::value_type;

  //~ std::cerr << 'i' << el << ' ' << th << std::endl;

  guard_type g(global_lock);

  return c.insert(map_element(el, el)).second;
}

bool erase(container_type& c, KeyT el)
{
  //~ std::cerr << 'e' << el << ' ' << th << std::endl;

  guard_type g(global_lock);

  return c.erase(el) == 1;
}


bool contains(container_type& c, KeyT el)
{
  guard_type g(global_lock);

  return c.find(el) != c.end();
}


size_t count(const container_type& c)
{
  return c.size();
}

container_type&
construct(const TestOptions&, const container_type*)
{
  container_type* cont = new container_type;

  if (cont == nullptr) throw std::runtime_error("could not allocate");

  return *cont;
}

container_type&
reconstruct(const TestOptions&, const container_type*)
{
  throw std::logic_error("recovery impossible");
}

}
#endif /* _TESTSTLMAP */
