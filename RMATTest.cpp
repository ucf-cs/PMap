#include "RMATTest.hpp"

// Input: 1- Array of threads that will execute a fucntion.
//        2- A function pointer to that function.
void threadRunner(std::thread *threads, void function(std::string filename))
{
    // Start our threads.
    for (size_t i = 0; i < RMAT_COUNT; i++)
    {
        std::string str = "./data/rmat/edge_list_rmat_s10_" + std::to_string(i) + "_of_" + std::to_string(RMAT_COUNT);
        threads[i] = std::thread(function, str);
    }

    // Set thread affinity.
    for (unsigned i = 0; i < RMAT_COUNT; ++i)
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
    for (size_t i = 0; i < RMAT_COUNT; i++)
    {
        threads[i].join();
    }

    return;
}

void reportDegree()
{
    for (size_t i = 0; i < NODE_COUNT; i++)
    {
        if (hashMap->containsKey(i << 3))
        {
            std::cout << "Node " << i << ":\t" << (hashMap->get(i) >> 3) << std::endl;
        }
    }
}

void parseFile(std::string filename)
{
    std::ifstream rmat(filename);
    if (!rmat.is_open())
    {
        throw std::runtime_error("Could not open file.");
    }
    std::string line;
    Key k;
    Value v;
    size_t val;
    while (std::getline(rmat, line))
    {
        std::stringstream ss(line);
        size_t colIdx = 0;
        while (ss >> val)
        {
            switch (colIdx)
            {
            case 0:
                k = val;
                break;
            case 1:
                v = val;
                break;
            default:
                throw std::runtime_error("Too many values found on this line.");
                break;
            }
            if (ss.peek() == ' ')
                ss.ignore();
            colIdx++;
        }
        hashMap->update(k, v, ConcurrentHashMap<Key, Value, xxhash<Key>>::Table::increment);
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
    std::thread threads[RMAT_COUNT];

    // Create the hash map.
    hashMap = new ConcurrentHashMap<Key, Value, xxhash<Key>>();

    // Get start time.
    auto start = std::chrono::high_resolution_clock::now();

    // Execute the transactions
    threadRunner(threads, parseFile);

    // Get end time.
    auto finish = std::chrono::high_resolution_clock::now();

    reportDegree();
    std::cout << "\n";
    std::cout << std::chrono::duration_cast<TIME_UNIT>(finish - start).count();
    std::cout << "\n";

    return 0;
}