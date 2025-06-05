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

#include "TPTask.h"
#include "TPBlockQueue.hpp"

namespace HSLL
{

#define HSLL_THREADPOOL_TIMEOUT 5

	/**
	 * @brief Thread pool implementation with multiple queues for task distribution
	 * @tparam T Type of task objects to be processed, must implement execute() method
	 */
	template <class T = TaskStack<>>
	class ThreadPool
	{
	private:
		TPBlockQueue<T> *queues;		  ///< Per-worker task queues
		unsigned int threadNum;			  ///< Number of worker threads/queues to create
		unsigned int queueLength;		  ///< Capacity of each internal queue
		std::vector<std::thread> workers; ///< Worker thread collection
		std::atomic<unsigned int> index;  ///< Atomic counter for round-robin task distribution to worker queues

	public:
		/**
		 * @brief Constructs an uninitialized thread pool
		 */
		ThreadPool() : queues(nullptr), threadNum(0), queueLength(0) {}

		/**
		 * @brief Initializes thread pool resources
		 * @param queueLength Capacity of each internal queue
		 * @param threadNum Number of worker threads/queues to create
		 * @param batchSize Maximum tasks to process per batch (min 1)
		 * @return true if initialization succeeded, false otherwise
		 */
		bool init(unsigned int queueLength, unsigned int threadNum, unsigned int batchSize = 1)
		{
			if (batchSize == 0 || threadNum == 0 || batchSize > queueLength)
				return false;

			queues = new (std::nothrow) TPBlockQueue<T>[threadNum];

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

			this->threadNum = threadNum;
			this->queueLength = queueLength;
			workers.reserve(threadNum);

			for (unsigned i = 0; i < threadNum; ++i)
			{
				workers.emplace_back([this, i, cores, batchSize]
									 {
					if (cores > 0)
					{
						unsigned id = i % cores;
						ThreadBinder::bind_current_thread_to_core(id);
					}
					worker(i, batchSize); });
			}

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
		 * @param count Number of tasks to create (Required: count <= queueLength)
		 * @return Actual number of tasks enqueued
		 */
		unsigned int emplaceBulk(unsigned int count)
		{
			assert(count <= queueLength);
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
		 * @param count Number of tasks to create (Required: count <= queueLength)
		 * @return Actual number of tasks enqueued
		 */
		template <BULK_CMETHOD METHOD = COPY, typename PACKAGE>
		unsigned int emplaceBulk(PACKAGE *packages, unsigned int count)
		{
			assert(count <= queueLength);
			unsigned int index = next_index();

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
		 * @param count Number of tasks in array (Required: count <= queueLength)
		 * @return Actual number of tasks enqueued
		 */
		template <BULK_CMETHOD METHOD = COPY>
		unsigned int append_bulk(T *tasks, unsigned int count)
		{
			assert(count <= queueLength);
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
						worker.join();
				}

				workers.clear();
				workers.shrink_to_fit();
				threadNum = 0;
				queueLength = 0;
				queues = nullptr;
				delete[] queues;
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
			return index.fetch_add(1, std::memory_order_relaxed) % threadNum;
		}

		/**
		 * @brief Worker thread processing function
		 */
		void worker(unsigned int index, unsigned batchSize) noexcept
		{
			std::vector<TPBlockQueue<T> *> other;
			other.reserve(threadNum - 1);

			for (unsigned i = 0; i < threadNum; ++i)
			{
				if (i != index)
					other.push_back(&queues[i]);
			}

			if (batchSize == 1)
			{
				process_single(std::ref(queues[index]), std::ref(other));
			}
			else
			{
				process_bulk(std::ref(queues[index]), std::ref(other), batchSize);
			}
		}

		/**
		 * @brief  Processes single task at a time
		 */
		static void process_single(TPBlockQueue<T> &queue, std::vector<TPBlockQueue<T> *> &other) noexcept
		{
			struct Stealer
			{
				unsigned int index;
				unsigned int total;
				unsigned int threshold;
				std::vector<TPBlockQueue<T> *> &other;

				Stealer(std::vector<TPBlockQueue<T> *> &other, unsigned int maxLength)
					: other(other), index(0), total(other.size()),
					  threshold(std::min(total, maxLength)) {}

				bool steal(T &element)
				{
					for (int i = 0; i < total; ++i)
					{
						unsigned int now = (index + i) % total;
						if (other[now]->size >= threshold)
						{
							if (other[now]->template pop<PLACE>(element))
							{
								index = now;
								return true;
							}
						}
					}
					return false;
				}
			};

			char storage[sizeof(T)];
			T *task = (T *)(&storage);
			Stealer stealer(other, queue.maxSize);

			if (!other.size())
			{
				while (queue.template wait_pop<PLACE>(*task))
				{
					task->execute();
					task->~T();
				}

				return;
			}

			while (true)
			{
				while (queue.template pop<PLACE>(*task))
				{
					task->execute();
					task->~T();

					if (queue.isStopped)
						return;
				}

				if (stealer.steal(*task))
				{
					task->execute();
					task->~T();
				}
				else
				{
					if (queue.template wait_pop<PLACE>(*task, std::chrono::milliseconds(HSLL_THREADPOOL_TIMEOUT)))
					{
						task->execute();
						task->~T();
					}
					else if (queue.isStopped)
					{
						return;
					}
				}
			}
		}

		/**
		 * @brief  Execute multiple tasks at a time
		 */
		static inline void execute_tasks(T *tasks, unsigned int count)
		{
			for (unsigned int i = 0; i < count; ++i)
			{
				tasks[i].execute();
				tasks[i].~T();
			}
		}

		/**
		 * @brief  Processes multiple tasks at a time
		 */
		static void process_bulk(TPBlockQueue<T> &queue, std::vector<TPBlockQueue<T> *> &other, unsigned batchSize) noexcept
		{
			struct Stealer
			{
				unsigned int index;
				unsigned int total;
				unsigned int batchSize;
				unsigned int threshold;
				std::vector<TPBlockQueue<T> *> &other;

				Stealer(std::vector<TPBlockQueue<T> *> &other, unsigned int batchSize, unsigned int maxLength)
					: other(other), index(0), total(other.size()), batchSize(batchSize),
					  threshold(std::min(batchSize * total, maxLength)) {}

				unsigned int steal(T *elements)
				{
					for (int i = 0; i < total; ++i)
					{
						unsigned int now = (index + i) % total;
						if (other[now]->size >= threshold)
						{
							unsigned int count = other[now]->template popBulk<PLACE>(elements, batchSize);
							if (count)
							{
								index = now;
								return count;
							}
						}
					}
					return 0;
				}
			};

			T *tasks = (T *)((void *)operator new[](batchSize * sizeof(T)));
			assert(tasks && "Failed to allocate task buffer");
			unsigned int count;
			Stealer stealer(other, batchSize, queue.maxSize);

			if (!other.size())
			{
				while (count = queue.template wait_popBulk<PLACE>(tasks, batchSize))
					execute_tasks(tasks, count);
			}
			else
			{
				while (true)
				{
					while (count = queue.template popBulk<PLACE>(tasks, batchSize))
					{
						execute_tasks(tasks, count);

						if (queue.isStopped)
							break;
					}

					count = stealer.steal(tasks);
					if (count)
					{
						execute_tasks(tasks, count);
					}
					else
					{
						count = queue.template wait_popBulk<PLACE>(tasks, batchSize, std::chrono::milliseconds(HSLL_THREADPOOL_TIMEOUT));

						if (count)
							execute_tasks(tasks, count);
						else if (queue.isStopped)
							break;
					}
				}
			}
			operator delete[](tasks);
		}
	};
}

#endif