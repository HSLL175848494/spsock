#ifndef HSLL_SPTASK
#define HSLL_SPTASK

#include "base/ThreadPool.hpp"
#include "SPTypes.h"
#include "noncopyable.h"

using namespace HSLL::DEFER;

namespace HSLL
{
    /**
     * @brief Socket read/write task structure for thread pool operations
     * @details Contains the execution context and callback function for socket I/O operations
     */
    struct SockTaskTcp
    {
        ReadWriteProc proc;  ///< Callback function for read/write operations
        SOCKController *ctx; ///< Socket controller context containing connection state

    public:
        ~SockTaskTcp() = default;
        SockTaskTcp() = default;

        /**
         * @brief Construct a socket task with specific context and handler
         * @param ctx Pointer to socket controller context
         * @param proc Callback function to handle I/O operations
         */
        SockTaskTcp(SOCKController *ctx, ReadWriteProc proc) : ctx(ctx), proc(proc) {};

        /**
         * @brief Execute the registered callback function
         * @note Wrapped through taskProc to handle error conditions
         */
        void execute()
        {
            proc(ctx);
        }
    };

    /**
     * @brief Single task submission utility
     * @note Manages individual task submission to thread pool with fallback handling
     */
    struct UtilTaskTcp_Single : noncopyable
    {
        bool flag = true;              ///< Submission availability flag
        ThreadPool<SockTaskTcp> *pool; ///< Associated thread pool instance

        /**
         * @brief Add a new task to the thread pool
         * @param ctx Socket controller context for the task
         * @param proc Callback function to execute
         * @note Reactivates processing through renableProc() if submission fails
         */
        void append(SOCKController *ctx, ReadWriteProc proc)
        {
            if (!flag)
                renableProc(ctx);

            if (!pool->emplace(ctx,proc))
            {
                flag = false;
                renableProc(ctx);
            }
        }
    };

    /**
     * @brief Batch task submission utility with circular buffer
     * @note Implements bulk submission with configurable batch sizes and failure rollback
     */
    struct UtilTaskTcp_Multipe : noncopyable
    {
        bool flag = true;              ///< Batch submission availability flag
        unsigned int back = 0;         ///< Circular buffer tail position
        unsigned int front = 0;        ///< Circular buffer head position
        unsigned int size = 0;         ///< Current tasks in buffer
        SockTaskTcp *tasks = nullptr;  ///< Circular buffer storage
        ThreadPool<SockTaskTcp> *pool; ///< Associated thread pool instance

        ~UtilTaskTcp_Multipe()
        {
            if (tasks)
                delete[] tasks;
        }

        /**
         * @brief Add task to buffer and trigger commit when full
         * @param ctx Socket controller context
         * @param proc Callback function
         */
        void append(SOCKController *ctx, ReadWriteProc proc)
        {
            if (!flag)
                renableProc(ctx);

            new (&tasks[front]) SockTaskTcp(ctx, proc);

            front = (front + 1) % tcpConfig.THREADPOOL_BATCH_SIZE_SUBMIT;
            size++;

            if (size == tcpConfig.THREADPOOL_BATCH_SIZE_SUBMIT)
                commit();
        }

        /**
         * @brief Submit buffered tasks to thread pool
         * @details Handles partial submission failures and reactivates unsubmitted tasks
         */
        void commit()
        {
            if (size == 0)
                return;

            unsigned int distance = tcpConfig.THREADPOOL_BATCH_SIZE_SUBMIT - back;
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
                unsigned int remaining = num - submitted;
                for (unsigned int i = 0; i < remaining; ++i)
                {
                    unsigned int index = (back + submitted + i) % tcpConfig.THREADPOOL_BATCH_SIZE_SUBMIT;
                    renableProc(tasks[index].ctx);
                }
                back = front;
                size = 0;
                flag = false;
                return;
            }

            size -= num;
            back = (back + num) % tcpConfig.THREADPOOL_BATCH_SIZE_SUBMIT;

            if (size)
                commit();
        }
    };

    /**
     * @brief Unified task submission interface
     * @details Automatically selects between single and batch submission modes
     *          based on global configuration settings
     */
    class UtilTaskTcp : noncopyable
    {
        UtilTaskTcp_Single ts;  ///< Single task handler
        UtilTaskTcp_Multipe tm; ///< Batch task handler

    public:
        UtilTaskTcp() = default;

        /**
         * @brief Initialize task submission system
         * @param pool Thread pool to use for task execution
         * @note Allocates batch buffer if configured for bulk operations
         */
        bool init(ThreadPool<SockTaskTcp> *pool)
        {
            if (tcpConfig.THREADPOOL_BATCH_SIZE_SUBMIT == 1)
            {
                ts.pool = pool;
            }
            else
            {
                tm.pool = pool;
                tm.tasks = new (std::nothrow) SockTaskTcp[tcpConfig.THREADPOOL_BATCH_SIZE_SUBMIT];
                if (tm.tasks == nullptr)
                    return false;
            }
            return true;
        }

        /**
         * @brief Add task to submission queue
         * @param ctx Socket controller context
         * @param proc Callback to execute
         */
        void append(SOCKController *ctx, ReadWriteProc proc)
        {
            if (tcpConfig.THREADPOOL_BATCH_SIZE_SUBMIT == 1)
                ts.append(ctx, proc);
            else
                tm.append(ctx, proc);
        }

        /**
         * @brief Reset submission state and flush remaining tasks
         * @note Should be called after submission failures to maintain consistency
         */
        void reset()
        {
            if (tcpConfig.THREADPOOL_BATCH_SIZE_SUBMIT == 1)
            {
                ts.flag = true;
            }
            else
            {
                tm.commit();
                tm.flag = true;
            }
        }
    };
}

#endif