#ifndef HSLL_THREADPOOL
#define HSLL_THREADPOOL

#if defined(__linux__)
#include <pthread.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace HSLL
{
	struct ThreadBinder
	{
		/**
		 * @brief Binds the calling thread to a specific CPU core
		 * @param id Target core ID (0-indexed)
		 * @return true if binding succeeded, false otherwise
		 *
		 * @note Platform-specific implementation:
		 *       - Linux: Uses pthread affinity
		 *       - Windows: Uses thread affinity mask
		 *       - Other platforms: No-op (always returns true)
		 */
		static bool bind_current_thread_to_core(unsigned id)
		{
#if defined(__linux__)
			cpu_set_t cpuset;
			CPU_ZERO(&cpuset);
			CPU_SET(id, &cpuset);
			return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0;

#elif defined(_WIN32)
			return SetThreadAffinityMask(GetCurrentThread(), 1ull << id) != 0;

#else
			return true;
#endif
		}
	};
};

#include <vector>
#include <thread>
#include <atomic>
#include <assert.h>
#include "TPBlockQueue.hpp"

namespace HSLL
{

	/**
	 * @brief Thread pool implementation with multiple queues for task distribution
	 * @tparam T Type of task objects to be processed, must implement execute() method
	 */
	template <class T>
	class ThreadPool
	{
	private:
		BlockQueue<T> *queues;			  ///< Per-worker task queues
		std::vector<std::thread> workers; ///< Worker thread collection
		unsigned int threadNum;			  ///< Number of worker threads/queues to create
		unsigned int queueLength;		  ///< Capacity of each internal queue
		std::atomic<unsigned int> index;  ///< Atomic counter for round-robin task distribution to worker queues

	public:
		/**
		 * @brief Constructs an uninitialized thread pool
		 */
		ThreadPool() : queues(nullptr) {}

		/**
		 * @brief Initializes thread pool resources
		 * @param queueLength Capacity of each internal queue
		 * @param threadNum Number of worker threads/queues to create
		 * @param batchSize Maximum tasks to process per batch (min 1)
		 * @return true if initialization succeeded, false otherwise
		 */
		bool init(unsigned int queueLength, unsigned int threadNum, unsigned int batchSize = 1)
		{

			if (batchSize == 0 || threadNum == 0)
				return false;

			queues = new (std::nothrow) BlockQueue<T>[threadNum];

			if (!queues)
				return false;

			for (unsigned i = 0; i < threadNum; ++i)
			{
				if (!queues[i].init(queueLength))
				{
					delete[] queues;
					queues = nullptr;
					return false;
				}
			}

			unsigned cores = std::thread::hardware_concurrency();

			if (!cores)
			{
				delete[] queues;
				queues = nullptr;
				return false;
			}

			workers.reserve(threadNum);
			for (unsigned i = 0; i < threadNum; ++i)
			{
				workers.emplace_back([i, cores, &queue = queues[i], batchSize]
									 {

					if (cores > 0)
					{
						unsigned id = i % cores;
						ThreadBinder::bind_current_thread_to_core(id);
					}
					worker(std::ref(queue), batchSize); });
			}

			this->threadNum = threadNum;
			this->queueLength = queueLength;

			return true;
		}

		/**
		 * @brief Enqueues a task using perfect forwarding
		 * @tparam Args Constructor argument types for task type T
		 * @param args Arguments to forward to task constructor
		 * @return true if task was enqueued successfully
		 */
		template <typename... Args>
		bool emplace(Args &&...args)
		{
			unsigned int index = next_index();

			if (queues[index].size < queueLength)
			{
				return queues[index].emplace(std::forward<Args>(args)...);
			}
			else
			{
				unsigned int half = threadNum / 2;
				return queues[(index + half) % threadNum].emplace(std::forward<Args>(args)...);
			}
		}

		/**
		 * @brief Enqueues multiple default-constructed tasks
		 * @param count Number of tasks to create
		 * @return Actual number of tasks enqueued
		 */
		unsigned int emplaceBulk(unsigned int count)
		{
			unsigned int index = next_index();

			if (queues[index].size + count / 2 <= queueLength)
			{
				return queues[index].template emplaceBulk(count);
			}
			else
			{
				unsigned int half = threadNum / 2;
				return queues[(index + half) % threadNum].template emplaceBulk(count);
			}
		}

		/**
		 * @brief Enqueues multiple tasks from parameter array
		 * @tparam METHOD Bulk construction method (COPY/MOVE)
		 * @tparam PACKAGE Type of construction parameters
		 * @param packages Construction parameter array
		 * @param count Number of tasks to create
		 * @return Actual number of tasks enqueued
		 */
		template <BULK_CMETHOD METHOD = COPY, typename PACKAGE>
		unsigned int emplaceBulk(PACKAGE *packages, unsigned int count)
		{
			unsigned int index = next_index();
			unsigned int half = threadNum / 2;

			if (queues[index].size + count / 2 <= queueLength)
			{
				return queues[index].template emplaceBulk<METHOD>(packages, count);
			}
			else
			{
				unsigned int half = threadNum / 2;
				return queues[(index + half) % threadNum].template emplaceBulk<METHOD>(packages, count);
			}
		}

		/**
		 * @brief Enqueues a single preconstructed task
		 * @tparam U Forwarding reference type (deduced)
		 * @param task Task object to enqueue
		 * @return true if task was enqueued successfully
		 */
		template <typename U>
		bool append(U &&task)
		{
			unsigned int index = next_index();

			if (queues[index].size < queueLength)
			{
				return queues[index].push(std::forward<U>(task));
			}
			else
			{
				unsigned int half = threadNum / 2;
				return queues[(index + half) % threadNum].push(std::forward<U>(task));
			}
		}

		/**
		 * @brief Enqueues multiple preconstructed tasks
		 * @tparam METHOD Bulk insertion method (COPY/MOVE)
		 * @param tasks Task object array
		 * @param count Number of tasks in array
		 * @return Actual number of tasks enqueued
		 */
		template <BULK_CMETHOD METHOD = COPY>
		unsigned int append_bulk(T *tasks, unsigned int count)
		{
			unsigned int index = next_index();

			if (queues[index].size + count / 2 <= queueLength)
			{
				return queues[index].template pushBulk<METHOD>(tasks, count);
			}
			else
			{
				unsigned int half = threadNum / 2;
				return queues[(index + half) % threadNum].template pushBulk<METHOD>(tasks, count);
			}
		}

		/**
		 * @brief Stops all workers and releases resources
		 */
		void exit() noexcept
		{
			if (queues)
			{
				for (unsigned i = 0; i < workers.size(); ++i)
				{
					queues[i].stopWait();
				}

				for (auto &worker : workers)
				{
					if (worker.joinable())
					{
						worker.join();
					}
				}

				workers.clear();
				delete[] queues;
				queues = nullptr;
			}
		}

		/**
		 * @brief Destroys thread pool and releases resources
		 */
		~ThreadPool()
		{
			exit();
		}

		// Deleted copy operations
		ThreadPool(const ThreadPool &) = delete;
		ThreadPool &operator=(const ThreadPool &) = delete;

	private:
		/**
		 * @brief Gets next queue index using round-robin
		 */
		unsigned int next_index() noexcept
		{
			return index.fetch_add(1, std::memory_order_relaxed) % workers.size();
		}

		/**
		 * @brief Worker thread processing function
		 */
		static void worker(BlockQueue<T> &queue, unsigned batchSize) noexcept
		{
			if (batchSize == 1)
			{
				process_single(queue);
			}
			else
			{
				process_bulk(queue, batchSize);
			}
		}

		/**
		 * @brief  Processes tasks one at a time
		 */
		static void process_single(BlockQueue<T> &queue) noexcept
		{
			std::aligned_storage_t<sizeof(T), alignof(T)> storage;
			T *task = (T *)(&storage);

			while (queue.wait_pop(*task))
			{
				task->execute();
				task->~T();
			}
		}

		/**
		 * @brief  Processes tasks in batches
		 */
		static void process_bulk(BlockQueue<T> &queue, unsigned batchSize) noexcept
		{
			T *tasks = (T *)(::operator new[](batchSize * sizeof(T)));
			assert(tasks && "Failed to allocate task buffer");

			while (true)
			{
				const unsigned count = queue.wait_popBulk(tasks, batchSize);
				if (count == 0)
					break;

				for (unsigned i = 0; i < count; ++i)
				{
					tasks[i].execute();
					tasks[i].~T();
				}
			}

			::operator delete[](tasks);
		}
	};
}

#endif