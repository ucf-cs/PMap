#ifndef _TESTUCFMAP
#define _TESTUCFMAP

#include "../cliff-map/hashMap.hpp"

namespace ucf
{
  struct container_type : ConcurrentHashMap<KeyT, ValT>
  {
    using base = ConcurrentHashMap<KeyT, ValT>;
    using base::base;
  };

  bool insert(container_type &c, ValT el)
  {
    ValT shiftedEl = el << 3;
    assert(shiftedEl >> 3 == el);

    // std::cout << "i " << shiftedEl << std::endl;

    ValT x = c.put(shiftedEl, shiftedEl);
    return x == shiftedEl;
  }

  bool erase(container_type &c, ValT el)
  {
    // std::cout << "e " << (el << 3) << std::endl;

    return c.remove(el << 3);
  }

  bool contains(container_type &c, KeyT el)
  {
    //~ std::cerr << 'e' << el << ' ' << th << std::endl;

    return c.containsKey(el);
  }

  ValT get(container_type &c, KeyT el)
  {
    return c.get(el);
  }

  size_t count(container_type &c)
  {
    return c.size();
  }

  ValT increment(container_type &c, KeyT el)
  {
    return c.update(el << 3, ((((size_t)1 << 61) - 3) << 3), container_type::Table::increment);
  }

  container_type &
  construct(const TestOptions &opt, container_type *)
  {
    const size_t realcapacity = 1 << opt.capacity;
    const char *fn = opt.filename.c_str();

    container_type *cont = new container_type(fn, realcapacity);

    if (cont == nullptr)
      throw std::runtime_error("could not allocate");

    return *cont;
  }

} // namespace ucf
#endif /* _TESTUCFMAP */
