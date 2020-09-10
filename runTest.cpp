// This file contains the primary test harness used for all testing.
// You must specify a test and a data structure.
// You must also specify test parameters, such as thread count and test size.
// Some tests will override test parameters by necessity (data type, thread count, etc.)

#include "runTest.hpp"

int main(int argc, char **args)
{
    test_type *test = new test_type();

    std::vector<std::string> arguments(args, args + argc);
    size_t argn = 1;
    bool matched = true;
    TestOptions settings;

    while (matched && (argn < arguments.size()))
    {
        matched = (matchOpt1(arguments, argn, "-t", settings.numthreads) ||
                   matchOpt1(arguments, argn, "-n", settings.numops) ||
                   matchOpt1(arguments, argn, "-p", settings.numruns) ||
                   matchOpt1(arguments, argn, "-c", settings.capacity) ||
                   matchOpt1(arguments, argn, "-f", settings.filename) ||
                   matchOpt0(arguments, argn, "-h", help, arguments.at(0)));
    }

    if (argn != arguments.size())
    {
        std::cerr << "unknown argument: " << arguments.at(argn) << std::endl;
        exit(1);
    }

    settings.print();

    try
    {
        size_t total_time = 0;

        for (size_t i = 1; i <= settings.numruns; ++i)
        {
            std::cout << "\n*****          test: " << i << std::endl;
            total_time += run_test(test, settings);
        }

        std::cout << "average time: " << (total_time / settings.numruns) << std::endl;
        std::cout << std::endl;
    }
    catch (const std::runtime_error &err)
    {
        std::cout << "error in test: " << err.what() << std::endl;
    }
    catch (...)
    {
        std::cout << "error in test..." << std::endl;
    }

    return 0;
}