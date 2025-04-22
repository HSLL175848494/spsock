#ifndef HSLL_SPTASK
#define HSLL_SPTASK

#include "SPTypes.h"
#include "ThreadPool.hpp"

namespace HSLL
{
    
    /**
     * @brief Function pointer for socket task processing
     * @details Acts as a mediator for delayed initialization of task handling logic.
     *          Initialized via SPInitializer::Init() to reference TaskFunc implementation.
     *          This indirection solves static initialization order issues by allowing
     *          binding to post-defined functions.
     */
    TaskProc taskProc;

    /**
     * @brief Function pointer for event re-enabling operations
     * @details Provides deferred initialization for connection recovery logic.
     *          Set through SPInitializer::Init() to point at REnableFunc implementation.
     *          The indirection enables proper initialization sequencing where function
     *          definitions become available before pointer assignment.
     */
    REnableProc renableProc;

    /**
     * @brief Read or write task for thread pool
     */
    struct SockTask
    {
        ReadWriteProc proc;  ///< Read/Write event callback
        SOCKController *ctx; ///< Context for task

    public:
        ~SockTask() = default;
        SockTask() = default;

        /**
         * @brief Construct a new SockTask object
         * @param ctx Context pointer for the task
         * @param proc Callback function to execute
         */
        SockTask(SOCKController *ctx, ReadWriteProc proc) : ctx(ctx), proc(proc) {};

        /**
         * @brief Execute the task's callback function
         */
        void execute()
        {
            taskProc(ctx, proc);
        }
    };

    /**
     * @brief Utility for handling single task operations
     */
    struct UtilTask_Single
    {
        FULL_LOAD_POLICY policy;    ///< Policy when thread pool is full
        ThreadPool<SockTask> *pool; ///< Pointer to thread pool

        /**
         * @brief Construct a new UtilTask_Single object
         * @param pool Pointer to the thread pool to use for task execution
         * @param policy The full load policy to apply when pool is at capacity
         */
        UtilTask_Single(ThreadPool<SockTask> *pool, FULL_LOAD_POLICY policy)
            : pool(pool), policy(policy) {}

        /**
         * @brief Append a task to the thread pool
         * @param ctx Context for the task
         * @param proc Callback function
         */
        void append(SOCKController *ctx, ReadWriteProc proc)
        {
            SockTask task(ctx, proc);
            if (!pool->append(task))
            {
                if (policy == FULL_LOAD_POLICY_WAIT)
                    pool->wait_append(task);
                else
                    renableProc(ctx);
            }
        }

        /**
         * @brief Submit tasks (no-op for single task)
         */
        void commit() {}
    };

    /**
     * @brief Utility for handling multiple tasks in batch
     */
    struct UtilTask_Multipe
    {
        unsigned int back = 0;                               ///< Back index of task buffer
        unsigned int front = 0;                              ///< Front index of task buffer
        unsigned int size = 0;                               ///< Current number of buffered tasks
        SockTask tasks[SPSOCK_THREADPOOL_BATCH_SIZE_SUBMIT]; ///< Task buffer

        FULL_LOAD_POLICY policy;    ///< Policy when thread pool is full
        ThreadPool<SockTask> *pool; ///< Pointer to thread pool

        /**
         * @brief Construct a new UtilTask_Multipe object
         * @param pool Pointer to the thread pool to use for task execution
         * @param policy The full load policy to apply when pool is at capacity
         */
        UtilTask_Multipe(ThreadPool<SockTask> *pool, FULL_LOAD_POLICY policy)
            : pool(pool), policy(policy) {}

        /**
         * @brief Append a task to the buffer
         * @param ctx Context for the task
         * @param proc Callback function
         */
        void append(SOCKController *ctx, ReadWriteProc proc)
        {
            tasks[front] = {ctx, proc};
            front = (front + 1) % SPSOCK_THREADPOOL_BATCH_SIZE_SUBMIT;
            size++;

            if (size == SPSOCK_THREADPOOL_BATCH_SIZE_SUBMIT)
                commit();
        }

        /**
         * @brief Submit buffered tasks to thread pool
         */
        void commit()
        {
            if (size == 0)
                return;

            unsigned int distance = SPSOCK_THREADPOOL_BATCH_SIZE_SUBMIT - back;
            unsigned int num = distance > size ? size : distance;
            unsigned int submitted;

            if (num == 1)
            {
                submitted = pool->append(tasks[back]) ? 1 : 0;
            }
            else
            {
                submitted = pool->append_bulk(tasks + back, num);
            }

            if (submitted != num)
            {
                if (policy == FULL_LOAD_POLICY_DISCARD)
                {
                    unsigned int remaining = num - submitted;
                    for (unsigned int i = 0; i < remaining; ++i)
                    {
                        unsigned int index = (back + submitted + i) % SPSOCK_THREADPOOL_BATCH_SIZE_SUBMIT;
                        renableProc(tasks[index].ctx);
                    }
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
                commit();
        }
    };

    /**
     * @brief Template utility to select task handler type
     * @tparam B Boolean template parameter to select type
     */
    template <bool B>
    struct UtilTask;

    /**
     * @brief Specialization for multiple task handling
     */
    template <>
    struct UtilTask<true>
    {
        using type = UtilTask_Multipe; ///< Type alias for multiple task handler
    };

    /**
     * @brief Specialization for single task handling
     */
    template <>
    struct UtilTask<false>
    {
        using type = UtilTask_Single; ///< Type alias for single task handler
    };
}

#endif