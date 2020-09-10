#ifndef RANDOM_HPP
#define RANDOM_HPP

#include "test.hpp"

// A random test.
// Preinserts elements equal to roughly half of the number of random operations performed.
// Randomly runs all available operations.
// Good for finding crash scenarios.
namespace randomTest
{
    struct test_type : Test
    {
        void container_test_prefix(ThreadInfo &ti)
        {
            size_t numops = ti.pnoiter;
            for (size_t i = 0; i < numops; i++)
            {
                // 50% prefill.
                if (rand() % 2)
                {
                    // Pick a random key/value.
                    size_t val = rand();
                    // UCF hash map has some reserved values that cannot be used.
                    if (typeid(container_type) == typeid(ucf::container_type))
                        // Keep trying until we fetch a non-reserved value.
                        while (ConcurrentHashMap<size_t, size_t>::isValueReserved(val << 3) || ConcurrentHashMap<size_t, size_t>::isKeyReserved(val << 3))
                        {
                            val = rand();
                        }
                    ((container_type *)ti.container)->insert(val);
                }
            }
            return;
        }
        void container_test(ThreadInfo &ti)
        {
            const size_t numops = opsPerThread(ti.num_threads, ti.pnoiter, ti.num);

            // TODO: Consider logging these results.
            for (size_t i = 0; i < numops; i++)
            {
                // Pick a random key/value.
                size_t val = rand();
                // UCF hash map has some reserved values that cannot be used.
                if (typeid(container_type) == typeid(ucf::container_type))
                    // Keep trying until we fetch a non-reserved value.
                    while (ConcurrentHashMap<size_t, size_t>::isValueReserved(val) || ConcurrentHashMap<size_t, size_t>::isKeyReserved(val))
                    {
                        val = rand();
                    }
                // TODO: Consider not performing rand() on the threads, and instead pre-calculate them.
                switch (rand() % 8)
                {
                case 0:
                    // Insert a value.
                    ((container_type *)ti.container)->insert(val);
                    break;
                case 1:
                    // Remove the value associated with this key.
                    ((container_type *)ti.container)->erase(val);
                    break;
                case 2:
                    // Check to see if there is a value associated with a specific key.
                    ((container_type *)ti.container)->contains(val);
                    break;
                case 3:
                    // Get the value currently associated with the current key.
                    ((container_type *)ti.container)->get(val);
                    break;
                case 4:
                    // Get the size of the hash map.
                    ((container_type *)ti.container)->count();
                    break;
                case 5:
                    // Increment the current value by 1.
                    ((container_type *)ti.container)->increment(val);
                    break;
                }
            }
        }
        void container_test_suffix(ThreadInfo &ti)
        {
            return;
        }
    };
} // namespace randomTest

#endif