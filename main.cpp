// This file includes methods that will be shared between all test cases

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
        // Choose a random object from the pool.
        size_t index = rand() % (THREAD_COUNT * NUM_OPS);
        void *ptr = &pointerPool[index];
        assert((uintptr_t)ptr % 8 == 0);
        // TODO: Consider not performing rand() on the threads, and instead pre-calculate them.
        switch (rand() % 7)
        {
        case 0:
            hashMap->size();
            break;
        case 1:
            hashMap->isEmpty();
            break;
        case 2:
            hashMap->containsKey(ptr);
            break;
        case 3:
            hashMap->put(ptr, ptr);
            break;
        case 4:
            hashMap->putIfAbsent(ptr, ptr);
            break;
        case 5:
            hashMap->remove(ptr);
            break;
        case 6:
            hashMap->replace(ptr, ptr, ptr);
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
            void *address = new char();
            hashMap->put(address, address);
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
    hashMap = new ConcurrentHashMap<void *, void *>();

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
    std::cout << std::chrono::duration_cast<std::chrono::TIME_UNIT>(finish - start).count();
    std::cout << "\n";

    return 0;
}