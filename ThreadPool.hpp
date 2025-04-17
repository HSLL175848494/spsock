#ifndef HSLL_THREADPOOL
#define HSLL_THREADPOOL

#include <vector>
#include <thread>
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
        unsigned int batchSize;           ///< Number of tasks to process in bulk operations
        BlockQueue<T> taskQueue;          ///< Thread-safe queue for storing pending tasks
        std::vector<std::thread> workers; ///< Collection of worker threads

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

            if (!taskQueue.init(queueSize))
                return false;

            for (unsigned i = 0; i < threadNum; ++i)
                workers.emplace_back(&ThreadPool::worker, this);

            return true;
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
         * @param tasks Pointer to array of tasks
         * @param count Number of tasks in the array
         * @return Number of tasks successfully added
         */
        unsigned int append_bulk(const T *tasks, unsigned int count)
        {
            return taskQueue.pushBulk(tasks, count);
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
         * @param tasks Pointer to array of tasks
         * @param count Number of tasks in the array
         * @return Number of tasks successfully added
         */
        unsigned int wait_append_bulk(const T *tasks, unsigned int count)
        {
            return taskQueue.wait_pushBulk(tasks, count);
        }

        /**
         * @brief Wait and append multiple tasks in bulk with timeout
         * @tparam Rep Time unit type for duration
         * @tparam Period Time interval type for duration
         * @param tasks Pointer to array of tasks
         * @param count Number of tasks in the array
         * @param timeout Maximum wait duration
         * @return Number of tasks successfully added before timeout
         */
        template <class Rep, class Period>
        unsigned int wait_append_bulk(const T *tasks, unsigned int count,
                                      const std::chrono::duration<Rep, Period> &timeout)
        {
            return taskQueue.wait_pushBulk(tasks, count, timeout);
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
                T *taskPtr = (T *)(&taskBuffer);

                while (true)
                {
                    if (taskQueue.pop(*taskPtr))
                    {
                        taskPtr->execute();
                        taskPtr->~T();
                    }
                    else
                    {
                        if (taskQueue.wait_pop(*taskPtr))
                        {
                            taskPtr->execute();
                            taskPtr->~T();
                        }
                        else
                        {
                            break;
                        }
                    }
                }
            }
            else
            {
                T *tasks = static_cast<T *>(operator new[](batchSize * sizeof(T)));

                while (true)
                {
                    unsigned int count = taskQueue.popBulk(tasks, batchSize);

                    if (count == 0)
                    {
                        count = taskQueue.wait_popBulk(tasks, batchSize);
                        if (count == 0)
                            break;
                    }

                    for (unsigned int i = 0; i < count; ++i)
                    {
                        tasks[i].execute();
                        tasks[i].~T();
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

        ThreadPool(const ThreadPool &) = delete;            ///< Disable copy constructor
        ThreadPool &operator=(const ThreadPool &) = delete; ///< Disable assignment operator
    };
}
#endif