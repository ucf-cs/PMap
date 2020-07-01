#include "define.hpp"
#include "pmwcas.hpp"

// Used to set process priority in Linux.
#include <sys/resource.h>
#include <unistd.h>

#define K 16

PMwCASManager<uintptr_t, K> *pmwcas;
alignas(64) PMwCASManager<uintptr_t, K>::Descriptor descriptors[NUM_OPS * THREAD_COUNT];

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
    for (size_t i = 0; i < NUM_OPS; i++)
    {
        PMwCASManager<uintptr_t, K>::Descriptor *desc = &descriptors[i + NUM_OPS * threadNum];
        desc->status.store(PMwCASManager<uintptr_t, K>::Status::Undecided);
        desc->count = rand() % K;

        // Determine the indexes to modify.
        std::set<size_t> indexes;
        // Keep getting more indexes until we have all that we need.
        while (indexes.size() < desc->count)
        {
            size_t index = rand() % ARRAY_SIZE;
            indexes.insert(index);
        }

        size_t j = 0;
        // Pulling from the list in order ensures fixed traversal.
        for (auto &&index : indexes)
        {
            desc->words[j].address = &array[index];
            desc->words[j].oldVal = pmwcas->PMwCASRead(&array[index]);
            // Avoid assigning an invalid, marked value.
            desc->words[j].newVal = rand() << 4;
            desc->words[j].mwcasDescriptor = desc;
            j++;
        }

        // Validate the descriptor.
        assert((desc->status.load() == PMwCASManager<uintptr_t, K>::Status::Undecided));
        assert(desc->count <= K);
        for (size_t j = 0; j < desc->count; j++)
        {
            assert(desc->words[j].address != NULL);
            assert((uintptr_t)desc->words[j].address > 100);
            assert((uintptr_t)desc->words[j].address >= ((uintptr_t)&array[0]));
            assert((uintptr_t)desc->words[j].address <= ((uintptr_t)&array[0] + (8 * ARRAY_SIZE)));
            assert(desc->words[j].mwcasDescriptor == desc);
        }

        // Perform the insertion.
        bool success = pmwcas->PMwCAS(desc);
        if (success)
        {
            std::cout << "Succeeded on operation " << i << " on thread " << threadNum << std::endl;
        }
        else
        {
            std::cout << "Failed on operation " << i << " on thread " << threadNum << std::endl;
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

    pmwcas = new PMwCASManager<uintptr_t, K>();

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
    std::cout << std::chrono::duration_cast<std::chrono::TIME_UNIT>(finish - start).count();
    std::cout << "\n";

    return 0;
}