#ifndef HSLL_THREADPOOL
#define HSLL_THREADPOOL

#include <vector>
#include <thread>
#include <atomic>
#include "BlockQueue.hpp"

namespace HSLL
{
    /**
     * @brief Thread pool class for managing worker threads and task execution
     * @tparam T Type of tasks to be processed by the thread pool
     */
    template <class T>
    class ThreadPool
    {
    private:
        unsigned int batchSize;
        BlockQueue<T> taskQueue;
        std::atomic<unsigned int> count;
        std::atomic<unsigned int> error;
        std::vector<std::thread> workers;

    public:
        ThreadPool() = default;

        /**
         * @brief Initialize the thread pool with specified parameters
         * @param queueSize Maximum capacity of the task queue
         * @param threadNum Number of worker threads to create
         * @param batchSize Number of tasks to process in batch (default=1)
         * @return true if initialization succeeded, false otherwise
         * @note Batch size must be greater than 0 for successful initialization
         */
        bool init(unsigned int queueSize, unsigned int threadNum, unsigned int batchSize = 1)
        {
            if (batchSize == 0)
                return false;

            this->batchSize = batchSize;
            count = 0;
            error = 0;

            if (!taskQueue.init(queueSize))
                return false;

            for (unsigned i = 0; i < threadNum; ++i)
                workers.emplace_back(&ThreadPool::worker, this);

            while (count != threadNum)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));

            if (error)
            {
                exit();
                return false;
            }

            return true;
        }

        /**
         * @brief Non-blocking element emplacement with perfect forwarding
         * @tparam Args Types of arguments to forward to element constructor
         * @param args Arguments to forward to element constructor
         * @return true if element was emplaced, false if queue was full
         * @details Constructs element in-place at the tail of the queue using
         *          perfect forwarding. Notifies consumers if successful.
         */
        template <typename... Args>
        bool emplace(Args &&...args)
        {
            return taskQueue.emplace(std::forward<Args>(args)...);
        }

        /**
         * @brief Blocking element emplacement with indefinite wait
         * @tparam Args Types of arguments to forward to element constructor
         * @param args Arguments to forward to element constructor
         * @return true if element was emplaced, false if queue was stopped
         * @details Waits until queue has space or is stopped. Constructs element
         *          in-place and notifies consumers upon success.
         */
        template <typename... Args>
        bool wait_emplace(Args &&...args)
        {
            return taskQueue.wait_emplace(std::forward<Args>(args)...);
        }

        /**
         * @brief Blocking element emplacement with timeout
         * @tparam Rep Chrono duration representation type
         * @tparam Period Chrono duration period type
         * @tparam Args Types of arguments to forward to element constructor
         * @param timeout Maximum duration to wait for space
         * @param args Arguments to forward to element constructor
         * @return true if element was emplaced, false on timeout or stop
         * @details Waits up to specified duration for space. Constructs element
         *          in-place if space becomes available and notifies consumers.
         */
        template <class Rep, class Period, typename... Args>
        bool wait_emplace(const std::chrono::duration<Rep, Period> &timeout, Args &&...args)
        {
            return taskQueue.wait_emplace(timeout, std::forward<Args>(args)...);
        }

        /**
         * @brief Non-blocking bulk default construction
         * @return Actual number of elements successfully created
         * @details Uses TYPE's default constructor. Fails immediately if queue
         *          lacks sufficient space. Notifies consumers appropriately.
         */
        unsigned int emplaceBulk(unsigned int count)
        {
            return taskQueue.emplaceBulk(count);
        }

        /**
         * @brief Non-blocking bulk construction from parameters array
         * @tparam METHOD BULK_CONSTRUCT_METHOD selection (COPY/MOVE)
         * @tparam PACKAGE Type of construction arguments for TYPE
         * @param packages Pointer to array of construction arguments
         * @param count Number of elements to construct
         * @return Actual number of elements successfully created
         * @details Constructs elements using TYPE's constructor that accepts PACKAGE.
         *          Uses specified construction semantics (copy/move). Notifies consumers
         *          with appropriate signal based on inserted quantity.
         */
        template <BULK_CMETHOD METHOD = COPY, typename PACKAGE>
        unsigned int emplaceBulk(PACKAGE *packages, unsigned int count)
        {
            return taskQueue.template emplaceBulk<METHOD>(packages, count);
        }

        /**
         * @brief Blocking bulk default construction with wait
         * @param count Number of default-constructed elements to create
         * @return Actual number of elements created before stop/full
         * @details Waits for space using TYPE's default constructor. Processes
         *          maximum available capacity when awakened. Notifies consumers
         *          based on inserted quantity.
         */
        unsigned int wait_emplaceBulk(unsigned int count)
        {
            return taskQueue.wait_emplaceBulk(count);
        }

        /**
         * @brief Blocking bulk construction from parameters array
         * @tparam METHOD BULK_CONSTRUCT_METHOD selection (COPY/MOVE)
         * @tparam PACKAGE Type of construction arguments for TYPE
         * @param packages Pointer to construction arguments array
         * @param count Number of elements to construct
         * @return Actual number of elements created before stop/full
         * @details Waits indefinitely until space becomes available. Constructs
         *          elements using specified construction semantics (copy/move).
         *          Returns immediately if queue is stopped.
         */
        template <BULK_CMETHOD METHOD = COPY, typename PACKAGE>
        unsigned int wait_emplaceBulk(PACKAGE *packages, unsigned int count)
        {
            return taskQueue.template wait_emplaceBulk<METHOD>(packages, count);
        }

        /**
         * @brief Timed bulk default construction
         * @tparam Rep Chrono duration representation type
         * @tparam Period Chrono duration period type
         * @param count Maximum elements to default-construct
         * @param timeout Maximum wait duration
         * @return Actual number of elements created
         * @details Combines timed wait with default construction. Processes
         *          maximum possible elements if space becomes available.
         *          Notifies consumers based on inserted quantity.
         */
        template <class Rep, class Period>
        unsigned int wait_emplaceBulk(unsigned int count,
                                      const std::chrono::duration<Rep, Period> &timeout)
        {
            return taskQueue.wait_emplaceBulk(count, timeout);
        }

        /**
         * @brief Timed bulk construction from parameters array
         * @tparam METHOD BULK_CONSTRUCT_METHOD selection (COPY/MOVE)
         * @tparam PACKAGE Type of construction arguments
         * @tparam Rep Chrono duration representation type
         * @tparam Period Chrono duration period type
         * @param packages Construction arguments array
         * @param count Maximum elements to construct
         * @param timeout Maximum wait duration
         * @return Actual number of elements constructed
         * @details Waits up to timeout duration for space. Constructs elements
         *          using specified construction semantics (copy/move). Returns
         *          immediately on timeout or queue stop.
         */
        template <BULK_CMETHOD METHOD = COPY, typename PACKAGE, class Rep, class Period>
        unsigned int wait_emplaceBulk(PACKAGE *packages, unsigned int count,
                                      const std::chrono::duration<Rep, Period> &timeout)
        {
            return taskQueue.template wait_emplaceBulk<METHOD>(packages, count, timeout);
        }

        /**
         * @brief Append a single task to the queue
         * @tparam U Forward reference type for task object
         * @param task Task object to be added
         * @return true if task was successfully added, false otherwise
         */
        template <typename U>
        bool append(U &&task)
        {
            return taskQueue.push(std::forward<U>(task));
        }

        /**
         * @brief Append multiple tasks to the queue in bulk
         * @tparam METHOD BULK_CONSTRUCT_METHOD selection (COPY/MOVE)
         * @param tasks Pointer to array of tasks
         * @param count Number of tasks in the array
         * @return Number of tasks successfully added
         * @details Uses specified construction semantics (copy/move) for bulk insertion
         */
        template <BULK_CMETHOD METHOD = COPY>
        unsigned int append_bulk(T *tasks, unsigned int count)
        {
            return taskQueue.template pushBulk<METHOD>(tasks, count);
        }

        /**
         * @brief Wait and append a single task to the queue
         * @tparam U Forward reference type for task object
         * @param task Task object to be added
         * @return true if task was successfully added before timeout, false otherwise
         */
        template <typename U>
        bool wait_append(U &&task)
        {
            return taskQueue.wait_push(std::forward<U>(task));
        }

        /**
         * @brief Wait and append a single task with timeout
         * @tparam U Forward reference type for task object
         * @tparam Rep Time unit type for duration
         * @tparam Period Time interval type for duration
         * @param task Task object to be added
         * @param timeout Maximum wait duration
         * @return true if task was successfully added before timeout, false otherwise
         */
        template <typename U, class Rep, class Period>
        bool wait_append(U &&task, const std::chrono::duration<Rep, Period> &timeout)
        {
            return taskQueue.wait_push(std::forward<U>(task), timeout);
        }

        /**
         * @brief Wait and append multiple tasks in bulk
         * @tparam METHOD BULK_CONSTRUCT_METHOD selection (COPY/MOVE)
         * @param tasks Pointer to array of tasks
         * @param count Number of tasks in the array
         * @return Number of tasks successfully added
         * @details Uses specified construction semantics (copy/move) for bulk insertion
         */
        template <BULK_CMETHOD METHOD = COPY>
        unsigned int wait_appendBulk(T *tasks, unsigned int count)
        {
            return taskQueue.template wait_pushBulk<METHOD>(tasks, count);
        }

        /**
         * @brief Wait and append multiple tasks in bulk with timeout
         * @tparam METHOD BULK_CONSTRUCT_METHOD selection (COPY/MOVE)
         * @tparam Rep Time unit type for duration
         * @tparam Period Time interval type for duration
         * @param tasks Pointer to array of tasks
         * @param count Number of tasks in the array
         * @param timeout Maximum wait duration
         * @return Number of tasks successfully added before timeout
         */
        template <BULK_CMETHOD METHOD = COPY, class Rep, class Period>
        unsigned int wait_appendBulk(T *tasks, unsigned int count,
                                     const std::chrono::duration<Rep, Period> &timeout)
        {
            return taskQueue.template wait_pushBulk<METHOD>(tasks, count, timeout);
        }

        /**
         * @brief Worker thread processing function
         * @note Handles both single task and bulk processing modes based on batchSize
         */
        void worker()
        {
            if (batchSize == 1)
            {
                std::aligned_storage_t<sizeof(T), alignof(T)> taskBuffer;
                T *taskPtr = reinterpret_cast<T *>(&taskBuffer);

                count++;

                while (true)
                {
                    if (taskQueue.wait_pop(*taskPtr))
                    {
                        taskPtr->execute();
                        conditional_destroy(*taskPtr);
                    }
                    else
                    {
                        break;
                    }
                }
            }
            else
            {
                T *tasks = static_cast<T *>(operator new[](batchSize * sizeof(T), std::nothrow));
                if (!tasks)
                {
                    error.store(1);
                    count++;
                    return;
                }

                count++;

                while (true)
                {
                    unsigned int popped = taskQueue.wait_popBulk(tasks, batchSize);
                    if (popped == 0)
                        break;

                    for (unsigned int i = 0; i < popped; ++i)
                    {
                        tasks[i].execute();
                        conditional_destroy(tasks[i]);
                    }
                }
                operator delete[](tasks);
            }
        }

        /**
         * @brief Stop all worker threads and clean up resources
         */
        void exit()
        {
            taskQueue.stopWait();
            for (auto &th : workers)
            {
                if (th.joinable())
                    th.join();
            }
            taskQueue.release();
        }

        ~ThreadPool()
        {
            exit();
        }

        ThreadPool(const ThreadPool &) = delete;
        ThreadPool &operator=(const ThreadPool &) = delete;
    };
}
#endif