#ifndef YCSB_HPP
#define YCSB_HPP

#include "test.hpp"

// An YCSB test.
// Preinserts elements, then runs various workload distributions, from file.
namespace YCSBTest
{
    const size_t operationCount = 16000000;
    struct op
    {
        typedef enum opType
        {
            INSERT,
            READ,
            DELETE,
            UPDATE
        } opType;
        opType operation;
        ValT val;
    };

    op **runQueue;
    size_t *move;

    struct test_type : Test
    {
        void container_test_prefix(ThreadInfo &ti)
        {
            FILE *ycsb, *ycsb_read;
            char buf[1024];
            char *pbuf = buf;
            size_t len = 1024;

            // Load phase.
            // TODO: Figure out a better way to pass in file name.
            if ((ycsb = fopen("/data/YCSB/outputLoada.txt", "r")) == nullptr)
            {
                printf("failed to read %s\n", "/data/YCSB/outputLoada.txt");
                exit(1);
            }
            while (getline(&pbuf, &len, ycsb) != -1)
            {
                if (strncmp(buf, "INSERT", 6) == 0)
                {
                    size_t scanVal;
                    sscanf(buf + 7, "%zu", &scanVal);
                    ValT val = (ValT)scanVal;
                    int succ = ((container_type *)ti.container)->insert(val);
                    assert(succ >= 0);
                    ++ti.succ;
                }
            }
            fclose(ycsb);

            // Now prepare data for the run phase.

            // Create a run queue to fill in.
            runQueue = new op *[ti.num_threads];
            for (size_t i = 0; i < ti.num_threads; ++i)
            {
                runQueue[i] = new op[operationCount];
            }

            // TODO: Figure out a better way to pass in file name.
            if ((ycsb_read = fopen("/data/YCSB/outputRuna.txt", "r")) == NULL)
            {
                printf("fail to read %s\n", "/data/YCSB/outputRuna.txt");
                exit(1);
            }

            move = new size_t[ti.num_threads];
            for (size_t t = 0; t < ti.num_threads; t++)
            {
                move[t] = 0;
            }

            size_t operation_num = 0;
            size_t scanVal;
            while (getline(&pbuf, &len, ycsb_read) != -1)
            {
                if (strncmp(buf, "INSERT", 6) == 0)
                {
                    sscanf(buf + 7, "%zu", &scanVal);
                    runQueue[operation_num % ti.num_threads][move[operation_num % ti.num_threads]].val = scanVal;
                    runQueue[operation_num % ti.num_threads][move[operation_num % ti.num_threads]].operation = op::opType::INSERT;
                    move[operation_num % ti.num_threads]++;
                }
                else if (strncmp(buf, "READ", 4) == 0)
                {
                    sscanf(buf + 5, "%zu", &scanVal);
                    runQueue[operation_num % ti.num_threads][move[operation_num % ti.num_threads]].val = scanVal;
                    runQueue[operation_num % ti.num_threads][move[operation_num % ti.num_threads]].operation = op::opType::READ;
                    move[operation_num % ti.num_threads]++;
                }
                else if (strncmp(buf, "DELETE", 6) == 0)
                {
                    sscanf(buf + 7, "%zu", &scanVal);
                    runQueue[operation_num % ti.num_threads][move[operation_num % ti.num_threads]].val = scanVal;
                    runQueue[operation_num % ti.num_threads][move[operation_num % ti.num_threads]].operation = op::opType::DELETE;
                    move[operation_num % ti.num_threads]++;
                }
                else if (strncmp(buf, "UPDATE", 6) == 0)
                {
                    sscanf(buf + 7, "%zu", &scanVal);
                    runQueue[operation_num % ti.num_threads][move[operation_num % ti.num_threads]].val = scanVal;
                    runQueue[operation_num % ti.num_threads][move[operation_num % ti.num_threads]].operation = op::opType::UPDATE;
                    move[operation_num % ti.num_threads]++;
                }
                assert(move[operation_num % ti.num_threads] <= operationCount);
                operation_num++;
            }
            fclose(ycsb_read);

            return;
        }
        void container_test(ThreadInfo &ti)
        {
            const size_t tinum = ti.num;
            const size_t maxops = opsPerThread(ti.num_threads, ti.pnoiter, 0);
            const size_t numops = opsPerThread(ti.num_threads, ti.pnoiter, tinum);
            size_t nummain = opsMainLoop(numops);
            size_t wrid = numops - nummain;
            size_t rdid = wrid / 2;

            for (size_t i = 0; i < move[tinum]; i++)
            {
                if (runQueue[tinum][i].operation == op::opType::INSERT)
                {
                    ((container_type *)ti.container)->insert(runQueue[tinum][i].val);
                }
                else if (runQueue[tinum][i].operation == op::opType::READ)
                {
                    ((container_type *)ti.container)->contains((KeyT)(runQueue[tinum][i].val));
                }
                else if (runQueue[tinum][i].operation == op::opType::DELETE)
                {
                    ((container_type *)ti.container)->erase(runQueue[tinum][i].val);
                }
                else if (runQueue[tinum][i].operation == op::opType::UPDATE)
                {
                    ((container_type *)ti.container)->insert(runQueue[tinum][i].val);
                }
                else
                {
                    printf("unknown clevel_op\n");
                    exit(1);
                }
            }
        }
        void container_test_suffix(ThreadInfo &ti)
        {
            return;
        }
    };
} // namespace YCSBTest

#endif