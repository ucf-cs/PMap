#include "define.hpp"
#include "pmwcas.hpp"

// Used to set process priority in Linux.
#include <sys/resource.h>
#include <unistd.h>

inline const size_t K = 64;

PMwCASManager<uintptr_t, K, THREAD_COUNT> *pmwcas;

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
    localThreadNum = threadNum;
    for (size_t i = 0; i < NUM_OPS; i++)
    {
        // A words array. Place words to modify here.
        PMwCASManager<uintptr_t, K, THREAD_COUNT>::Word words[K];

        // The number of words we will modify.
        size_t count = (rand() % K) + 1;

        // Determine the indexes to modify.
        std::unordered_set<size_t> indexes;
        // Keep getting more indexes until we have all that we need.
        while (indexes.size() < count)
        {
            size_t index = rand() % ARRAY_SIZE;
            indexes.insert(index);
        }

        // Fill in the words.
        size_t j = 0;
        for (auto &&index : indexes)
        {
            // We choose an index of the array to modify, which was selected at random.
            words[j].address = &array[index];
            // Read the current value at that index.
            // If it doesn't match, the PMwCAS will fail.
            words[j].oldVal = pmwcas->PMwCASRead(&array[index]);
            // Pick a new value at random.
            // By keeping the last 3 bits at 0, we avoid assigning an invalid, marked value.
            words[j].newVal = rand() << 3;
            j++;
        }

        // Perform the insertion.
        bool success = pmwcas->PMwCAS(threadNum, count, words);
        if (success)
        {
            //std::cout << "Succeeded on operation " << i << " on thread " << threadNum << std::endl;
        }
        else
        {
            //std::cout << "Failed on operation " << i << " on thread " << threadNum << std::endl;
        }
    }
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

    // Create a PMwCAS manager.
    // All PMwCAS operations should run through here.
    pmwcas = new PMwCASManager<uintptr_t, K, THREAD_COUNT>();

    // Ensure DescRef is word-sizes.
    assert(sizeof(PMwCASManager<uintptr_t, K, THREAD_COUNT>::DescRef) == 8);

    // Zero out the array.
    for (size_t i = 0; i < ARRAY_SIZE; i++)
    {
        array[i].store(0);
    }

    // Test casting.
    PMwCASManager<uintptr_t, K, THREAD_COUNT>::DescRef::testCast();

    // Get start time.
    auto start = std::chrono::high_resolution_clock::now();

    // Execute the transactions
    threadRunner(threads, performOps);

    // Get end time.
    auto finish = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < ARRAY_SIZE; i++)
    {
        std::cout << array[i].load() << "\n";
    }

    std::cout << "\n";
    std::cout << std::chrono::duration_cast<TIME_UNIT>(finish - start).count();
    std::cout << "\n";

    return 0;
}