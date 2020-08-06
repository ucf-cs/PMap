// A collection of STL-compliant hash functions.

#include "xxhash.hpp"

// xxhash is a fast hashing library.
template <class Type>
class xxhash
{
public:
    size_t operator()(const Type &type) const
    {
        std::array<Type, 1> input{type};
        xxh::hash_t<64> hash = xxh::xxhash<64>(input);
        return ((size_t)hash);
    }
};

// This hash just returns the key itself, assuming Type is 64 bits or fewer.
template <class Type>
class NaiveHash
{
public:
    size_t operator()(const Type &type) const
    {
        return ((size_t)type);
    }
};

// The STL hash is already STL-compliant, so there's no need to make a wrapper for this.
// STL Hash.
//std::hash<Key>{}(key);