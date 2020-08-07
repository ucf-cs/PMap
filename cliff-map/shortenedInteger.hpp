#ifndef __SHORTENEDINTEGER_H__
#define __SHORTENEDINTEGER_H__

#include <cstddef>
#include <iostream>

// TODO: This class is meant to hide the use of bit marking while otherwise representing an unsigned 61-bit integer.
// For now, it remains incomplete and unused.
struct ShortenedInt
{
public:
    size_t integer : 61;
    // Reserved bits.
    bool isRDCSS : 1;
    bool isPMwCAS : 1;
    bool isDirty : 1;

    static ShortenedInt toShortenedInt(size_t other)
    {
        ShortenedInt val;
        val.integer = other;
        val.isRDCSS = false;
        val.isPMwCAS = false;
        val.isDirty = false;
        return val;
    }

    bool operator==(const ShortenedInt &other)
    {
        return integer == other.integer;
    }
    ShortenedInt &operator=(const int &other)
    {
        integer = other;
    }
    ShortenedInt operator+(const ShortenedInt &other)
    {
        ShortenedInt val;
        val.integer = integer + other.integer;
        val.isRDCSS = false;
        val.isPMwCAS = false;
        val.isDirty = false;
        return val;
    }
};

std::ostream &operator<<(std::ostream &os, const ShortenedInt &myObject)
{
    os << myObject.integer;
    return os;
}

#endif