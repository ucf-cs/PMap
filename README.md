# PMap
A persistent concurrent hash map

## How to build and run
This code has been written to build and run in a Linux environment. In particular, it uses a Linux-specific header library to set thread priority and affinity. All testing was done on Ubuntu 18.04.4 LTS. 

Before building the code, set the number of threads (`THREAD_COUNT`) to use and the number of operations (`NUM_OPS`) to perform by modifying the definitions in `define.hpp`. These can be adjusted and the code rebuilt as needed. By default, the testing framework will run an even distribution of operations after prefilling with 50% of `NUM_OPS`. 

To compile the code, run the following command using GCC:

`g++ -std=c++17 -pthread main.cpp`

This will prodice a program named `a.out`. You can run this code with the following command:

`./a.out`

The program outputs the full contents of the hash table, followed by the program runtime in microseconds.

## Project state
This project currently uses an implementation of *A Lock-Free Wait-Free Hash Table* by Cliff Click. It uses xxhash as a fast hashing algorithm. It performs put, contains, and remove operations in the benchmark, but offers support for additional operations. The design presently uses linear probing and offers no persistence. The next step of this project is to add efficient KCAS support. From there, lock-free hopscotch hashing can be added. After that, KCAS can be replaced with an implementation of PMwCAS. Detailed references to these components can be found in the comments at the top of `hashMap.hpp`. 