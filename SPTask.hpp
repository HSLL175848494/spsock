#ifndef HSLL_SPTASK
#define HSLL_SPTASK

#include "ThreadPool.hpp"

namespace HSLL
{

#define SPSOCK_THREADPOOL_QUEUE_LENGTH 10000    ///< The maximum number of task queue
#define SPSOCK_THREADPOOL_DEFAULT_THREADS_NUM 4 ///< If the number of system cores fails to be obtained, set the default number of threads
#define SPSOCK_THREADPOOL_BATCH_SIZE_SUBMIT 10 ///< Number of tasks to submit in batch
#define SPSOCK_THREADPOOL_BATCH_SIZE_PROCESS 5 ///< Number of tasks to process in batch

    enum FULL_LOAD_POLICY
    {
        FULL_LOAD_POLICY_WAIT,
        FULL_LOAD_POLICY_ABANDON
    };

    typedef void (*RWProc)(void *ctx); ///< Read/Write event callback type

    /**
     * @brief Read or write task for thread pool
     */
    struct SockTask
    {
        void *ctx;   ///< Context for task
        RWProc proc; ///< Read/Write event callback

    public:
        ~SockTask() = default;
        SockTask() = default;
        SockTask(void *ctx, RWProc proc) : ctx(ctx), proc(proc) {};
        void execute() { proc(ctx); }
    };

    struct UtilTask_Single
    {
        FULL_LOAD_POLICY policy;
        ThreadPool<SockTask> *pool;

        void append(void *ctx, RWProc proc)
        {
            SockTask task(ctx, proc);
            if (!pool->append(task))
                pool->wait_append(task);
        }

        void submit() {}
    };

    struct UtilTask_Multipe
    {
        unsigned int back = 0;
        unsigned int front = 0;
        unsigned int size = 0;
        SockTask tasks[SPSOCK_THREADPOOL_BATCH_SIZE_SUBMIT];

        FULL_LOAD_POLICY policy;
        ThreadPool<SockTask> *pool;

        void append(void *ctx, RWProc proc)
        {
            tasks[front] = {ctx, proc};
            front = (front + 1) % SPSOCK_THREADPOOL_BATCH_SIZE_SUBMIT;
            size++;

            if (size == SPSOCK_THREADPOOL_BATCH_SIZE_SUBMIT)
                submit();
        }

        void submit()
        {
            if (size == 0)
                return;

            unsigned int distance = SPSOCK_THREADPOOL_BATCH_SIZE_SUBMIT - back;
            unsigned int num = distance > size ? size : distance;
            unsigned int submitted;

            if (num == 1)
                submitted = pool->append(tasks[back]) ? 1 : 0;
            else
                submitted = pool->append_bulk(tasks + back, num);

            if (submitted != num)
            {
                if (policy == FULL_LOAD_POLICY_ABANDON)
                {
                    back = front;
                    size = 0;
                    return;
                }
                else
                {
                    unsigned int num2 = num - submitted;

                    if (num2 == 1)
                        pool->wait_append(tasks[back + submitted]);
                    else
                        pool->wait_append_bulk(tasks + back + submitted, num2);
                }
            }

            size -= num;
            back = (back + num) % SPSOCK_THREADPOOL_BATCH_SIZE_SUBMIT;

            if (size)
                submit();
        }
    };

    template <bool>
    struct UtilTask;

    template <>
    struct UtilTask<true>
    {
        using type = UtilTask_Multipe;
    };

    template <>
    struct UtilTask<false>
    {
        using type = UtilTask_Single;
    };
}

#endif