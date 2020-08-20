// NOTE: Change this and the #define TEST to change the test.
//#include "test-unique-elems.hpp"
#include "test-rmat.hpp"

int main(int argc, char **args)
{
    TEST test;

    std::vector<std::string> arguments(args, args + argc);
    size_t argn = 1;
    size_t seqtime = 0;
    bool matched = true;
    TestOptions settings;

    while (matched && (argn < arguments.size()))
    {
        matched = (test.matchOpt1(arguments, argn, "-n", settings.numops) ||
                   test.matchOpt1(arguments, argn, "-f", settings.filename) ||
                   test.matchOpt1(arguments, argn, "-t", settings.numthreads) ||
                   test.matchOpt1(arguments, argn, "-p", settings.numruns) ||
                   test.matchOpt1(arguments, argn, "-c", settings.capacity) ||
                   test.matchOpt0(arguments, argn, "-s", TEST::setField<bool>, std::ref(settings.sequential), true) ||
                   test.matchOpt0(arguments, argn, "-d", TEST::setField<bool>, std::ref(settings.printdata), true) ||
                   test.matchOpt0(arguments, argn, "-h", TEST::help, arguments.at(0)));
    }

    if (argn != arguments.size())
    {
        std::cerr << "unknown argument: " << arguments.at(argn) << std::endl;
        exit(1);
    }

    std::cout << "*** concurrent container test "
              << "\n*** total number of operations: " << settings.numops
              << "\n***          number of threads: " << settings.numthreads
              << "\n***                mapped file: " << settings.filename
              << "\n***      initial capacity base: " << settings.capacity
              << " ( " << (1 << settings.capacity) << ")"
              << "\n***             container type: " << typeid(container_type).name()
              << std::endl;

    if (settings.printdata)
        test.prnGen(settings.numthreads, settings.numops);

    if (settings.sequential)
    {
        try
        {
            std::cout << "\n***** sequential test" << std::endl;
            test.prepare_test(settings);
            seqtime = test.sequential_test(settings);
        }
        catch (...)
        {
            std::cout << "error in sequential test..." << std::endl;
        }
    }

    try
    {
        size_t total_time = 0;

        for (size_t i = 1; i <= settings.numruns; ++i)
        {
            std::cout << "\n***** parallel test: " << i << std::endl;
            test.prepare_test(settings);
            total_time += test.parallel_test(settings);
        }

        if (settings.numruns)
        {
            std::cout << "average time: " << (total_time / settings.numruns) << std::endl;
        }

        std::cout << std::endl;
    }
    catch (const std::runtime_error &err)
    {
        std::cout << "error in parallel test: " << err.what() << std::endl;
    }
    catch (...)
    {
        std::cout << "error in parallel test..." << std::endl;
    }

    test.unused(seqtime);
    return 0;
}