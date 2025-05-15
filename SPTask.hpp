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

            SockTaskTcp task(ctx, proc);

            if (!pool->append(task))
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

    /**
     * @brief UDP task structure for thread pool operations
     * @details Contains the context, data buffer, and processing function for UDP packet handling
     */
    struct SockTaskUdp
    {
        char *data;              ///< Pointer to received data buffer
        size_t size;             ///< Size of received data buffer
        char ip[46];             ///< Source IP address of UDP packet (IPv4/IPv6 compatible)
        unsigned short port = 0; ///< Source port of UDP packet

    public:
        /**
         * @brief Default constructor
         */
        SockTaskUdp() : data(nullptr) {}

        /**
         * @brief Construct a complete UDP task
         * @param data Received data buffer (ownership transferred)
         * @param size Size of data buffer
         * @param ip Source IP address string
         * @param port Source port number
         */
        SockTaskUdp(char *data, size_t size, const char *ip, unsigned short port)
            : data(data), size(size), port(port)
        {
            strcpy(this->ip, ip);
        }

        /**
         * @brief Move constructor for efficient resource transfer
         * @param other Source task to move from
         */
        SockTaskUdp(SockTaskUdp &&other) : data(other.data), size(other.size), port(other.port)
        {
            strcpy(ip, other.ip);
            other.data = nullptr;
        }

        /**
         * @brief Move assignment operator
         * @param other Source task to move from
         * @return Reference to this object
         */
        SockTaskUdp &operator=(SockTaskUdp &&other)
        {
            if (this == &other)
                return *this;

            if (data)
            {
                funcFree(data);
                data = nullptr;
            }

            data = other.data;
            size = other.size;
            port = other.port;
            strcpy(ip, other.ip);
            other.data = nullptr;
            return *this;
        }

        /**
         * @brief Destructor - ensures proper buffer cleanup
         */
        ~SockTaskUdp()
        {
            if (data)
                funcFree(data);
        }

        /**
         * @brief Execute the registered callback function
         * @details Transfers ownership of data buffer to callback function
         */
        void execute()
        {
            funcRecv(recvCtx, data, size, ip, port);
            data = nullptr; // Ownership transferred
        }
    };

    /**
     * @brief Single UDP task submission utility
     * @note Handles individual task submission to thread pool
     */
    struct UtilTaskUdp_Single : noncopyable
    {
        ThreadPool<SockTaskUdp> *pool; ///< Associated thread pool instance

        /**
         * @brief Add a new UDP task to the thread pool
         * @param data Received data buffer (ownership transferred)
         * @param size Data buffer size
         * @param ip Source IP address
         * @param port Source port
         */
        void append(char *data, size_t size, const char *ip, unsigned short port)
        {
            SockTaskUdp task(data, size, ip, port);
            pool->append(std::move(task));
        }
    };

    /**
     * @brief Batch UDP task submission utility with circular buffer
     * @note Implements bulk submission with configurable batch sizes and time-based auto-commit
     */
    struct UtilTaskUdp_Multipe : noncopyable
    {
        unsigned int back = 0;                      ///< Circular buffer tail position (oldest task)
        unsigned int front = 0;                     ///< Circular buffer head position (next insertion slot)
        unsigned int size = 0;                      ///< Current number of buffered tasks
        SockTaskUdp *tasks = nullptr;               ///< Circular buffer storage array
        ThreadPool<SockTaskUdp> *pool;              ///< Associated thread pool instance
        std::chrono::steady_clock::time_point last; ///< Last commit timestamp for auto-flushing

        /**
         * @brief Destructor - ensures proper cleanup of buffered tasks
         */
        ~UtilTaskUdp_Multipe()
        {
            if (tasks)
            {
                unsigned int index = back;
                for (unsigned int i = 0; i < size; i++)
                {
                    tasks[index].~SockTaskUdp();
                    index = (index + 1) % udpConfig.THREADPOOL_BATCH_SIZE_SUBMIT;
                }

                delete[] tasks;
            }
        }

        /**
         * @brief Add UDP task to buffer and trigger commit when full
         * @param data Received data buffer (ownership transferred)
         * @param size Data buffer size
         * @param ip Source IP address
         * @param port Source port
         */
        void append(char *data, size_t size, const char *ip, unsigned short port)
        {
            new (&tasks[front]) SockTaskUdp(data, size, ip, port);
            front = (front + 1) % udpConfig.THREADPOOL_BATCH_SIZE_SUBMIT;
            this->size++;

            if (this->size == udpConfig.THREADPOOL_BATCH_SIZE_SUBMIT)
            {
                last = std::chrono::steady_clock::now();
                commit();
            }
        }

        /**
         * @brief Submit buffered tasks to thread pool
         * @details Handles partial submission failures and cleans up unsubmitted tasks.
         *          Implements automatic retry of remaining tasks on failure.
         */
        void commit()
        {
            if (size == 0)
                return;

            unsigned int distance = udpConfig.THREADPOOL_BATCH_SIZE_SUBMIT - back;
            unsigned int num = (distance > size) ? size : distance;
            unsigned int submitted = 0;

            if (num > 1)
            {
                submitted = pool->append_bulk(&tasks[back], num);
            }
            else
            {
                submitted = pool->append(std::move(tasks[back])) ? 1 : 0;
            }

            if (submitted < num)
            {
                const unsigned int unsubmitted = num - submitted;
                for (unsigned int i = 0; i < unsubmitted; i++)
                {
                    const unsigned int idx = (back + submitted + i) % udpConfig.THREADPOOL_BATCH_SIZE_SUBMIT;
                    tasks[idx].~SockTaskUdp();
                }
            }

            back = (back + submitted) % udpConfig.THREADPOOL_BATCH_SIZE_SUBMIT;
            size -= submitted;

            if (size > 0)
                commit();
        }
    };

    /**
     * @brief Unified UDP task submission interface
     * @details Automatically selects between single and batch submission modes
     *          based on configuration. Implements time-based auto-commit for batch mode.
     */
    class UtilTaskUdp : noncopyable
    {
        UtilTaskUdp_Single ts;  ///< Single task submission handler
        UtilTaskUdp_Multipe tm; ///< Batch task submission handler

    public:
        /**
         * @brief Initialize UDP task submission system
         * @param pool Thread pool for task execution
         * @return true if initialization succeeded, false otherwise
         */
        bool init(ThreadPool<SockTaskUdp> *pool)
        {
            if (udpConfig.THREADPOOL_BATCH_SIZE_SUBMIT == 1)
            {
                ts.pool = pool;
            }
            else
            {
                tm.pool = pool;
                tm.last = std::chrono::steady_clock::now();
                tm.tasks = new (std::nothrow) SockTaskUdp[udpConfig.THREADPOOL_BATCH_SIZE_SUBMIT];
                if (!tm.tasks)
                    return false;
            }
            return true;
        }

        /**
         * @brief Add UDP task to submission system
         * @param data Received data buffer (ownership transferred)
         * @param size Data buffer size
         * @param ip Source IP address
         * @param port Source port
         */
        void append(void *ctx, char *data, size_t size, const char *ip, unsigned short port)
        {
            if (udpConfig.THREADPOOL_BATCH_SIZE_SUBMIT == 1)
            {
                ts.append(data, size, ip, port);
            }
            else
            {
                tm.append(data, size, ip, port);
            }
        }

        /**
         * @brief Commit remaining buffered tasks
         * @note Implements time-based auto-commit with 10ms interval
         */
        void commit()
        {
            if (udpConfig.THREADPOOL_BATCH_SIZE_SUBMIT != 1)
            {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - tm.last).count() >= 10)
                {
                    tm.commit();
                    tm.last = now;
                }
            }
        }
    };
}

#endif