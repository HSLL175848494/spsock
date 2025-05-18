#ifndef HSLL_BLOCKQUEUE
#define HSLL_BLOCKQUEUE

#include <new>
#include <mutex>
#include <chrono>
#include <condition_variable>

namespace HSLL
{

#if defined(__GNUC__) || defined(__clang__)
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif

	/**
	 * @brief Helper template for conditional object destruction
	 * @tparam T Type of object to manage
	 * @tparam IsTrivial Boolean indicating if type is trivially destructible
	 * @details Provides destruction mechanism that respects type traits:
	 *          - For non-trivial types: calls object destructor explicitly
	 *          - For trivial types: no-operation
	 */
	template <typename T, bool IsTrivial = std::is_trivially_destructible<T>::value>
	struct DestroyHelper {
		static void destroy(T& obj) { obj.~T(); }
	};

	/**
	 * @brief Specialization for trivially destructible types
	 */
	template <typename T>
	struct DestroyHelper<T, true> {
		static void destroy(T&) {}
	};

	/**
	 * @brief Conditionally destroys an object based on its type traits
	 * @tparam T Type of the object to potentially destroy
	 * @param obj Reference to the object being managed
	 * @details Uses DestroyHelper to decide whether to call destructor:
	 *          - Calls destructor for non-trivial types
	 *          - No action for trivial types
	 */
	template <typename T>
	void conditional_destroy(T& obj) {
		DestroyHelper<T>::destroy(obj);
	}

	/**
	 * @brief Circular buffer based blocking queue implementation
	 * @tparam TYPE Element type stored in the queue
	 * @details Features:
	 *          - Fixed-size circular buffer using preallocated nodes
	 *          - Single mutex design for all operations
	 *          - Size tracking for quick capacity checks
	 *          - Optimized for types with trivial destructors
	 *          - Efficient memory layout for cache locality
	 */
	template <class TYPE>
	class BlockQueue
	{
	private:

		struct Node
		{
			TYPE data;  ///< Storage for element data
			Node* next; ///< Pointer to next node in circular list
			Node() = default;
		};

		// Memory management
		void* memoryBlock;             ///< Raw memory block for node storage
		unsigned int isStopped;	       ///< Flag for stopping all operations

		// Queue state tracking
		unsigned int size;             ///< Current number of elements in queue
		unsigned int maxSize;          ///< Maximum capacity of the queue

		// List pointers
		Node* dataListHead;            ///< Pointer to first element in queue
		Node* dataListTail;            ///< Pointer to next insertion position

		// Synchronization primitives
		std::mutex dataMutex;          ///< Mutex protecting all queue operations
		std::condition_variable notEmptyCond; ///< Signaled when data becomes available
		std::condition_variable notFullCond;  ///< Signaled when space becomes available

	public:

		BlockQueue()
			: memoryBlock(nullptr),
			isStopped(0) {}

		~BlockQueue() { release(); }

		/**
		 * @brief Initializes queue with fixed capacity
		 * @param capacity Maximum number of elements the queue can hold
		 * @return true if initialization succeeded, false otherwise
		 * @details Creates circular linked list from preallocated nodes
		 */
		bool init(unsigned int capacity)
		{
			if (memoryBlock || capacity == 0)
				return false;

#if defined(_WIN32)
			memoryBlock = _aligned_malloc(sizeof(Node) * capacity, 64);
#else
			memoryBlock = aligned_alloc(64, sizeof(Node) * capacity);
#endif

			if (!memoryBlock)
				return false;

			Node* nodes = (Node*)(memoryBlock);

			for (unsigned int i = 0; i < capacity - 1; ++i)
				nodes[i].next = &nodes[i + 1];
			nodes[capacity - 1].next = &nodes[0];

			size = 0;
			maxSize = capacity;
			dataListHead = nodes;
			dataListTail = nodes;
			return true;
		}

		/**
		 * @brief Non-blocking element push
		 * @tparam T Deduced element type (supports perfect forwarding)
		 * @param element Element to push into queue
		 * @return true if element was added, false if queue was full
		 */
		template <class T>
		bool push(T&& element)
		{
			std::unique_lock<std::mutex> lock(dataMutex);

			if (UNLIKELY(size == maxSize))
				return false;

			new (&dataListTail->data) TYPE(std::forward<T>(element));
			size++;
			dataListTail = dataListTail->next;
			lock.unlock();
			notEmptyCond.notify_one();
			return true;
		}

		/**
		 * @brief Blocking element push with indefinite wait
		 * @tparam T Deduced element type (supports perfect forwarding)
		 * @param element Element to push into queue
		 * @return true if element was added, false if queue was stopped
		 */
		template <class T>
		bool wait_push(T&& element)
		{
			std::unique_lock<std::mutex> lock(dataMutex);
			notFullCond.wait(lock, [this] {
				return LIKELY(size != maxSize) || UNLIKELY(isStopped);
				});

			if (UNLIKELY(size == maxSize))
				return false;

			new (&dataListTail->data) TYPE(std::forward<T>(element));
			size++;
			dataListTail = dataListTail->next;
			lock.unlock();
			notEmptyCond.notify_one();
			return true;
		}

		/**
		 * @brief Blocking element push with timeout
		 * @tparam T Deduced element type
		 * @tparam Rep Chrono duration representation type
		 * @tparam Period Chrono duration period type
		 * @param element Element to push into queue
		 * @param timeout Maximum time to wait for space
		 * @return true if element was added, false on timeout or stop
		 */
		template <class T, class Rep, class Period>
		bool wait_push(T&& element, const std::chrono::duration<Rep, Period>& timeout)
		{
			std::unique_lock<std::mutex> lock(dataMutex);
			bool success = notFullCond.wait_for(lock, timeout, [this] {
				return LIKELY(size != maxSize) || UNLIKELY(isStopped);
				});

			if (UNLIKELY(!success || size == maxSize))
				return false;

			new (&dataListTail->data) TYPE(std::forward<T>(element));
			size++;
			dataListTail = dataListTail->next;
			lock.unlock();
			notEmptyCond.notify_one();
			return true;
		}

		/**
		 * @brief Non-blocking element removal
		 * @param element Reference to store popped element
		 * @return true if element was retrieved, false if queue was empty
		 */
		bool pop(TYPE& element)
		{
			std::unique_lock<std::mutex> lock(dataMutex);

			if (UNLIKELY(!size))
				return false;

			size--;
			element = std::move(dataListHead->data);
			conditional_destroy(dataListHead->data);
			dataListHead = dataListHead->next;
			lock.unlock();
			notFullCond.notify_one();
			return true;
		}

		/**
		 * @brief Blocking element removal with indefinite wait
		 * @param element Reference to store popped element
		 * @return true if element was retrieved, false if queue was stopped
		 */
		bool wait_pop(TYPE& element)
		{
			std::unique_lock<std::mutex> lock(dataMutex);
			notEmptyCond.wait(lock, [this] {
				return LIKELY(size) || UNLIKELY(isStopped);
				});

			if (UNLIKELY(!size))
				return false;

			size--;
			element = std::move(dataListHead->data);
			conditional_destroy(dataListHead->data);
			dataListHead = dataListHead->next;
			lock.unlock();
			notFullCond.notify_one();
			return true;
		}

		/**
		 * @brief Blocking element removal with timeout
		 * @tparam Rep Chrono duration representation type
		 * @tparam Period Chrono duration period type
		 * @param element Reference to store popped element
		 * @param timeout Maximum time to wait for data
		 * @return true if element was retrieved, false on timeout or stop
		 */
		template <class Rep, class Period>
		bool wait_pop(TYPE& element, const std::chrono::duration<Rep, Period>& timeout)
		{
			std::unique_lock<std::mutex> lock(dataMutex);
			bool success = notEmptyCond.wait_for(lock, timeout, [this] {
				return LIKELY(size) || UNLIKELY(isStopped);
				});

			if (UNLIKELY(!success || !size))
				return false;

			size--;
			element = std::move(dataListHead->data);
			conditional_destroy(dataListHead->data);
			dataListHead = dataListHead->next;
			lock.unlock();
			notFullCond.notify_one();
			return true;
		}

		/**
		 * @brief Bulk push for multiple elements
		 * @param elements Array of elements to push
		 * @param count Number of elements to push
		 * @return Actual number of elements pushed
		 */
		unsigned int pushBulk(TYPE* elements, unsigned int count)
		{
			if (UNLIKELY(count == 0))
				return 0;

			std::unique_lock<std::mutex> lock(dataMutex);
			unsigned int available = maxSize - size;

			if (UNLIKELY(available == 0))
				return 0;

			unsigned int toPush = std::min(count, available);
			for (unsigned int i = 0; i < toPush; ++i)
			{
				new (&dataListTail->data) TYPE(std::move(elements[i]));
				dataListTail = dataListTail->next;
			}

			size += toPush;
			lock.unlock();

			if (LIKELY(toPush == 1))
				notEmptyCond.notify_one();
			else
				notEmptyCond.notify_all();

			return toPush;
		}

		/**
		 * @brief Blocking bulk push with indefinite wait
		 * @param elements Array of elements to push
		 * @param count Number of elements to push
		 * @return Actual number of elements pushed before stop
		 */
		unsigned int wait_pushBulk(TYPE* elements, unsigned int count)
		{
			if (UNLIKELY(count == 0))
				return 0;

			std::unique_lock<std::mutex> lock(dataMutex);
			notFullCond.wait(lock, [this] {
				return LIKELY(size != maxSize) || UNLIKELY(isStopped);
				});

			if (UNLIKELY(size == maxSize))
				return 0;

			unsigned int available = maxSize - size;
			unsigned int toPush = std::min(count, available);
			for (unsigned int i = 0; i < toPush; ++i)
			{
				new (&dataListTail->data) TYPE(std::move(elements[i]));
				dataListTail = dataListTail->next;
			}

			size += toPush;
			lock.unlock();

			if (LIKELY(toPush == 1))
				notEmptyCond.notify_one();
			else
				notEmptyCond.notify_all();

			return toPush;
		}

		/**
		 * @brief Blocking bulk push with timeout
		 * @tparam Rep Chrono duration representation type
		 * @tparam Period Chrono duration period type
		 * @param elements Array of elements to push
		 * @param count Number of elements to push
		 * @param timeout Maximum time to wait for space
		 * @return Actual number of elements pushed
		 */
		template <class Rep, class Period>
		unsigned int wait_pushBulk(TYPE* elements, unsigned int count, const std::chrono::duration<Rep, Period>& timeout)
		{
			if (UNLIKELY(count == 0))
				return 0;

			std::unique_lock<std::mutex> lock(dataMutex);
			bool success = notFullCond.wait_for(lock, timeout, [this] {
				return LIKELY(size != maxSize) || UNLIKELY(isStopped);
				});

			if (UNLIKELY(!success || size == maxSize))
				return 0;

			unsigned int available = maxSize - size;
			unsigned int toPush = std::min(count, available);
			for (unsigned int i = 0; i < toPush; ++i)
			{
				new (&dataListTail->data) TYPE(std::move(elements[i]));
				dataListTail = dataListTail->next;
			}

			size += toPush;
			lock.unlock();

			if (LIKELY(toPush == 1))
				notEmptyCond.notify_one();
			else
				notEmptyCond.notify_all();

			return toPush;
		}

		/**
		 * @brief Bulk element retrieval
		 * @param elements Array to store retrieved elements
		 * @param count Maximum number of elements to retrieve
		 * @return Actual number of elements retrieved
		 */
		unsigned int popBulk(TYPE* elements, unsigned int count)
		{
			if (UNLIKELY(count == 0))
				return 0;

			std::unique_lock<std::mutex> lock(dataMutex);
			unsigned int available = size;

			if (UNLIKELY(available == 0))
				return 0;

			unsigned int toPop = std::min(count, available);
			for (unsigned int i = 0; i < toPop; ++i)
			{
				elements[i] = std::move(dataListHead->data);
				conditional_destroy(dataListHead->data);
				dataListHead = dataListHead->next;
			}

			size -= toPop;
			lock.unlock();

			if (LIKELY(toPop == 1))
				notFullCond.notify_one();
			else
				notFullCond.notify_all();

			return toPop;
		}

		/**
		 * @brief Blocking bulk retrieval with indefinite wait
		 * @param elements Array to store retrieved elements
		 * @param count Maximum number of elements to retrieve
		 * @return Actual number of elements retrieved before stop
		 */
		unsigned int wait_popBulk(TYPE* elements, unsigned int count)
		{
			if (UNLIKELY(count == 0))
				return 0;

			std::unique_lock<std::mutex> lock(dataMutex);
			notEmptyCond.wait(lock, [this] {
				return LIKELY(size) || UNLIKELY(isStopped);
				});

			if (UNLIKELY(!size))
				return 0;

			unsigned int toPop = std::min(count, size);
			for (unsigned int i = 0; i < toPop; ++i)
			{
				elements[i] = std::move(dataListHead->data);
				conditional_destroy(dataListHead->data);
				dataListHead = dataListHead->next;
			}

			size -= toPop;
			lock.unlock();

			if (LIKELY(toPop == 1))
				notFullCond.notify_one();
			else
				notFullCond.notify_all();

			return toPop;
		}

		/**
		 * @brief Blocking bulk retrieval with timeout
		 * @tparam Rep Chrono duration representation type
		 * @tparam Period Chrono duration period type
		 * @param elements Array to store retrieved elements
		 * @param count Maximum number of elements to retrieve
		 * @param timeout Maximum time to wait for data
		 * @return Actual number of elements retrieved
		 */
		template <class Rep, class Period>
		unsigned int wait_popBulk(TYPE* elements, unsigned int count, const std::chrono::duration<Rep, Period>& timeout)
		{
			if (UNLIKELY(count == 0))
				return 0;

			std::unique_lock<std::mutex> lock(dataMutex);
			bool success = notEmptyCond.wait_for(lock, timeout, [this] {
				return LIKELY(size) || UNLIKELY(isStopped);
				});

			if (UNLIKELY(!success || !size))
				return 0;

			unsigned int toPop = std::min(count, size);
			for (unsigned int i = 0; i < toPop; ++i)
			{
				elements[i] = std::move(dataListHead->data);
				conditional_destroy(dataListHead->data);
				dataListHead = dataListHead->next;
			}

			size -= toPop;
			lock.unlock();

			if (LIKELY(toPop == 1))
				notFullCond.notify_one();
			else
				notFullCond.notify_all();

			return toPop;
		}

		/**
		 * @brief Signals all waiting threads to stop blocking
		 * @details Sets the stop flag and notifies all condition variables
		 */
		void stopWait()
		{
			{
				std::lock_guard<std::mutex> lock(dataMutex);
				isStopped = 1;
			}

			notEmptyCond.notify_all();
			notFullCond.notify_all();
		}

		/**
		 * @brief Releases all resources and resets queue state
		 * @details Frees memory block and resets to initial state
		 */
		void release()
		{
			if (memoryBlock)
			{
				Node* nodes = (Node*)memoryBlock;
				Node* current = dataListHead;

				for (unsigned int i = 0; i < size; ++i)
				{
					conditional_destroy(current->data);
					current = current->next;
				}

#if defined(_WIN32)
				_aligned_free(memoryBlock);
#else
				free(memoryBlock);
#endif

				isStopped = 0;
				memoryBlock = nullptr;
			}
		}

		// Disable copying
		BlockQueue(const BlockQueue&) = delete;
		BlockQueue& operator=(const BlockQueue&) = delete;
	};
}
#endif // HSLL_BLOCKQUEUE