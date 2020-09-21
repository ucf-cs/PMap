#ifndef _TESTONEFILEMAP
#define _TESTONEFILEMAP 1

#define PWB_IS_CLFLUSHOPT 1
#include "ptms/PMDKTM.hpp"
#include "ptms/OneFilePTMLF.hpp"
#include "pdatastructures/TMHashMap.hpp"


namespace onefile
{

// using PTM    = poflf::OneFileLF;
// using TMTYPE = poflf::tmtype;

using PTM = pmdk::PMDKTM;

template <class T>
using TMTYPE = pmdk::persist<T>;

struct container_type : TMHashMap<KeyT, ValT, PTM, TMTYPE>
{
  using base = TMHashMap<KeyT, ValT, PTM, TMTYPE>;
  using base::base;
};


bool insert(container_type& c, ValT val)
{
  return c.add(val);
}

bool erase(container_type& c, KeyT val)
{
  return c.remove(val);
}

bool contains(container_type& c, KeyT val)
{
  return c.contains(val);
}

size_t count(const container_type& c)
{
  return c.size();
}

container_type&
construct(const TestOptions& opt, const container_type*)
{
  container_type* cont = nullptr;

  PTM::template updateTx<bool>([&] () {
            const size_t realcapacity = 1 << opt.capacity;

            cont = PTM::template tmNew<container_type>(realcapacity);
            return true;
        });

  if (cont == nullptr) throw std::runtime_error("could not allocate");

  return *cont;
}

container_type&
reconstruct(const TestOptions&, const container_type*)
{
  throw std::logic_error("recovery not implemented");
}

}
#endif /* _TESTONEFILEMAP */
