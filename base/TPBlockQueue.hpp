#ifndef HSLL_TPBLOCKQUEUE
#define HSLL_TPBLOCKQUEUE

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
	struct DestroyHelper
	{
		static void destroy(T &obj) { obj.~T(); }
	};

	/**
	 * @brief Specialization for trivially destructible types
	 */
	template <typename T>
	struct DestroyHelper<T, true>
	{
		static void destroy(T &) {}
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
	void conditional_destroy(T &obj)
	{
		DestroyHelper<T>::destroy(obj);
	}

	/**
	 * @brief Enumeration defining the method of bulk construction
	 * This enum is used to specify whether bulk construction operations should use
	 * copy semantics or move semantics when constructing objects.
	 */
	enum BULK_CMETHOD
	{
		COPY, ///< Use copy construction semantics
		MOVE  ///< Use move construction semantics
	};

	/**
	 * @brief Helper template for bulk construction (copy/move)
	 * @tparam T Type of object to construct
	 * @tparam Method BULK_CONSTRUCT_METHOD selection
	 * @tparam U Type of the source object
	 */
	template <typename T, BULK_CMETHOD Method, typename U>
	struct BulkConstructHelper;

	/**
	 * @brief Specialization for copy construction from type U to T
	 */
	template <typename T, typename U>
	struct BulkConstructHelper<T, COPY, U>
	{
		static void construct(T *ptr, U &source)
		{
			new (ptr) T(source);
		}
	};

	/**
	 * @brief Specialization for move construction from type U to T
	 */
	template <typename T, typename U>
	struct BulkConstructHelper<T, MOVE, U>
	{
		static void construct(T *ptr, U &source)
		{
			new (ptr) T(std::move(source));
		}
	};

	/**
	 * @brief Conditionally constructs an object using copy/move semantics
	 * @tparam Method BULK_CONSTRUCT_METHOD selection
	 * @tparam T Type of the object to construct
	 * @tparam U Type of the source object
	 * @param ptr Pointer to memory location where object should be constructed
	 * @param source Source object reference for construction
	 */
	template <BULK_CMETHOD Method, typename T, typename U>
	void bulk_construct(T *ptr, U &source)
	{
		BulkConstructHelper<T, Method, U>::construct(ptr, source);
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
			TYPE data;	///< Storage for element data
			Node *next; ///< Pointer to next node in circular list
			Node() = default;
		};

		// Memory management
		void *memoryBlock;		///< Raw memory block for node storage
		unsigned int isStopped; ///< Flag for stopping all operations

		// Queue state tracking
		unsigned int size;	  ///< Current number of elements in queue
		unsigned int maxSize; ///< Maximum capacity of the queue

		// List pointers
		Node *dataListHead; ///< Pointer to first element in queue
		Node *dataListTail; ///< Pointer to next insertion position

		// Synchronization primitives
		std::mutex dataMutex;				  ///< Mutex protecting all queue operations
		std::condition_variable notEmptyCond; ///< Signaled when data becomes available
		std::condition_variable notFullCond;  ///< Signaled when space becomes available

		template <class T>
		friend class ThreadPool;

	public:
		BlockQueue() : memoryBlock(nullptr), isStopped(0) {}

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

			Node *nodes = (Node *)(memoryBlock);

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
			std::unique_lock<std::mutex> lock(dataMutex);
			if (UNLIKELY(size == maxSize))
				return false;

			new (&dataListTail->data) TYPE(std::forward<Args>(args)...);
			size++;
			dataListTail = dataListTail->next;
			lock.unlock();
			notEmptyCond.notify_one();
			return true;
		}

		/**
		 * @brief Bulk construction from parameters array with specified method
		 * @tparam METHOD BULK_CONSTRUCT_METHOD selection (copy/move)
		 * @param packages Construction parameters array
		 * @param count Maximum elements to construct
		 * @return Actual number of elements constructed
		 * @details Uses TYPE's constructor with parameter type PACKAGE. Construction
		 *          method (copy/move) is determined by METHOD template parameter.
		 */
		template <BULK_CMETHOD METHOD = COPY, typename PACKAGE>
		unsigned int emplaceBulk(PACKAGE *packages, unsigned int count)
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
				bulk_construct<METHOD>(&dataListTail->data, packages[i]);
				dataListTail = dataListTail->next;
			}

			size += toPush;
			lock.unlock();

			if (LIKELY(toPush > 0))
			{
				if (toPush == 1)
					notEmptyCond.notify_one();
				else
					notEmptyCond.notify_all();
			}

			return toPush;
		}

		/**
		 * @brief Non-blocking bulk default construction
		 * @param count Number of default-constructed elements to create
		 * @return Actual number of elements successfully created
		 * @details Uses TYPE's default constructor. Fails immediately if queue
		 *          lacks sufficient space. Notifies consumers appropriately.
		 */
		unsigned int emplaceBulk(unsigned int count)
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
				new (&dataListTail->data) TYPE();
				dataListTail = dataListTail->next;
			}

			size += toPush;
			lock.unlock();

			if (LIKELY(toPush > 0))
			{
				if (toPush == 1)
					notEmptyCond.notify_one();
				else
					notEmptyCond.notify_all();
			}

			return toPush;
		}

		/**
		 * @brief Non-blocking element push
		 * @tparam T Deduced element type (supports perfect forwarding)
		 * @param element Element to push into queue
		 * @return true if element was added, false if queue was full
		 */
		template <class T>
		bool push(T &&element)
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
		 * @brief Bulk push for multiple elements using specified construction method
		 * @param elements Array of elements to construct from
		 * @param count Number of elements to push
		 * @return Actual number of elements pushed
		 * @details Constructs elements using either copy or move semantics based on
		 *          BULK_CONSTRUCT_METHOD template parameter. Notifies consumers based
		 *          on inserted quantity.
		 */
		template <BULK_CMETHOD METHOD = COPY>
		unsigned int pushBulk(TYPE *elements, unsigned int count)
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
				bulk_construct<METHOD>(&dataListTail->data, elements[i]);
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
		 * @brief Blocking element removal with indefinite wait
		 * @param element Reference to store popped element
		 * @return true if element was retrieved, false if queue was stopped
		 */
		bool wait_pop(TYPE &element)
		{
			std::unique_lock<std::mutex> lock(dataMutex);
			notEmptyCond.wait(lock, [this]
							  { return LIKELY(size) || UNLIKELY(isStopped); });

			if (UNLIKELY(!size))
				return false;

			size--;
			element = std::move(dataListHead->data);
			conditional_destroy(dataListHead->data);
			dataListHead = dataListHead->next;
			lock.unlock();
			return true;
		}

		/**
		 * @brief Blocking bulk retrieval with indefinite wait
		 * @param elements Array to store retrieved elements
		 * @param count Maximum number of elements to retrieve
		 * @return Actual number of elements retrieved before stop
		 */
		unsigned int wait_popBulk(TYPE *elements, unsigned int count)
		{
			if (UNLIKELY(count == 0))
				return 0;

			std::unique_lock<std::mutex> lock(dataMutex);
			notEmptyCond.wait(lock, [this]
							  { return LIKELY(size) || UNLIKELY(isStopped); });

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
				Node *nodes = (Node *)memoryBlock;
				Node *current = dataListHead;

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
		BlockQueue(const BlockQueue &) = delete;
		BlockQueue &operator=(const BlockQueue &) = delete;
	};
}
#endif // HSLL_TPBLOCKQUEUE