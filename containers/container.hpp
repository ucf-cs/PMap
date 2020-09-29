// This header defines all functions required by all containers to support tests.
#ifndef CONTAINER_HPP
#define CONTAINER_HPP

#include "define.hpp"

class Container
{
public:
    // Insert a value.
    virtual bool insert(ValT el) = 0;
    // Remove a value.
    virtual bool erase(ValT el) = 0;
    // Check for the existance of a value associated with a key.
    virtual bool contains(KeyT el) = 0;
    // Retrieve the value associated with a key.
    virtual ValT get(KeyT el) = 0;
    // Retrieve the number of elements logically in the data structure.
    virtual size_t count() = 0;
    // Increment the value associated with the key by one.
    virtual ValT increment(KeyT el) = 0;
    // Internal data structure validation.
    // This is highly unique to each data structure.
    virtual bool isConsistent() = 0;
};

#endif