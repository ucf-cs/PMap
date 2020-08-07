#include "main.hpp"

// Input: 1- Array of threads that will execute a fucntion.
//        2- A function pointer to that function.
void threadRunner(std::thread *threads, void function(int threadNum))
{
    // Start our threads.
    for (size_t i = 0; i < THREAD_COUNT; i++)
    {
        threads[i] = std::thread(function, i);
    }

    // Set thread affinity.
    for (unsigned i = 0; i < THREAD_COUNT; ++i)
    {
        // Create a cpu_set_t object representing a set of CPUs. Clear it and mark only CPU i as set.
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(i, &cpuset);
        int rc = pthread_setaffinity_np(threads[i].native_handle(), sizeof(cpu_set_t), &cpuset);
        if (rc != 0)
        {
            std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
        }
    }

    // Wait for all threads to complete.
    for (size_t i = 0; i < THREAD_COUNT; i++)
    {
        threads[i].join();
    }

    return;
}

// Performs a bunch of random operations.
void performOps(int threadNum)
{
    // TODO: Consider logging these results.
    for (size_t i = 0; i < NUM_OPS; i++)
    {
        // Pick a random key/value.
        size_t val = rand() << 3;
        // Keep trying until we fetch a non-reserved value.
        while (ConcurrentHashMap<size_t, size_t>::isKeyReserved(val) ||
               ConcurrentHashMap<size_t, size_t>::isValueReserved(val))
        {
            val = rand() << 3;
        }
        // Make sure we have an unmarked key/value.
        assert((uintptr_t)val % 8 == 0);
        // TODO: Consider not performing rand() on the threads, and instead pre-calculate them.
        switch (rand() % 8)
        {
        case 0:
            // Get the size of the hash map.
            hashMap->size();
            break;
        case 1:
            // Check if the hash map is empty.
            hashMap->isEmpty();
            break;
        case 2:
            // Check to see if there is a value associated with a specific key.
            hashMap->containsKey(val);
            break;
        case 3:
            // Insert a value.
            hashMap->put(val, val);
            break;
        case 4:
            // Insert a value, but do not replace an existing value.
            hashMap->putIfAbsent(val, val);
            break;
        case 5:
            // Remove the value associated with this key.
            hashMap->remove(val);
            break;
        case 6:
            // Insert a value only if it will replace an old value.
            hashMap->replace(val, val, rand() << 3);
            break;
        case 7:
            // An arbitrary update function.
            // In this case, increment the current value by 1.
            hashMap->update(val, 1 << 3, ConcurrentHashMap<Key, Value, xxhash<Key>>::Table::increment);
            break;
        }
    }
}

// Pre-inserts a bunch of keys and values into the hash map.
void preinsert(int threadNum)
{
    for (size_t i = 0; i < NUM_OPS; i++)
    {
        // 50% prefill.
        if (rand() % 2)
        {
            // Pick a random key/value.
            size_t val = rand() << 3;
            // Keep trying until we fetch a non-reserved value.
            while (ConcurrentHashMap<size_t, size_t>::isValueReserved(val) || ConcurrentHashMap<size_t, size_t>::isKeyReserved(val))
            {
                val = rand() << 3;
            }
            hashMap->put(val, val);
            //std::cout << std::endl;
            //hashMap->print();
        }
    }
    return;
}

int setMaxPriority()
{
    int which = PRIO_PROCESS;
    id_t pid;
    int priority = -20;
    int ret;

    pid = getpid();
    ret = setpriority(which, pid, priority);
    return ret;
}

int main(void)
{
    // Seed the random number generator.
    srand(time(NULL));

    // Ensure the test process runs at maximum priority.
    // Only works if run under sudo permissions.
    setMaxPriority();

    // Create our threads.
    std::thread threads[THREAD_COUNT];

    // Create the hash map.
    hashMap = new ConcurrentHashMap<size_t, size_t, xxhash<Key>>(TABLE_SIZE);

    // Pre-insertion step.
    //threadRunner(threads, preinsert);
    // Single-threaded alternative.
    for (size_t i = 0; i < THREAD_COUNT; i++)
    {
        preinsert(i);
    }

    // Get start time.
    auto start = std::chrono::high_resolution_clock::now();

    // Execute the transactions
    threadRunner(threads, performOps);

    // Get end time.
    auto finish = std::chrono::high_resolution_clock::now();

    hashMap->print();
    std::cout << "\n";
    std::cout << std::chrono::duration_cast<TIME_UNIT>(finish - start).count();
    std::cout << "\n";

    return 0;
}