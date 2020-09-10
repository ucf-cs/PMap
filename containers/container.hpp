// This header defines all functions required by all containers to support tests.
#ifndef CONTAINER_HPP
#define CONTAINER_HPP

#include "define.hpp"

class Container
{
public:
    virtual bool insert(ValT el) = 0;
    virtual bool erase(ValT el) = 0;
    virtual bool contains(KeyT el) = 0;
    virtual ValT get(KeyT el) = 0;
    virtual size_t count() = 0;
    virtual ValT increment(KeyT el) = 0;
};

#endif