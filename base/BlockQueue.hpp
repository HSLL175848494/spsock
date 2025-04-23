#ifndef HSLL_BLOCKQUEUE
#define HSLL_BLOCKQUEUE

#include <new>
#include <mutex>
#include <atomic>
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
	 * @brief Thread-safe fixed-size block queue with blocking operations
	 * @tparam TYPE Type of elements stored in the queue
	 * @details Implements a producer-consumer pattern using:
	 *          - Preallocated node pool for memory efficiency
	 *          - Double-linked lists for data and free nodes
	 *          - Separate mutexes for data and free lists
	 *          - Condition variables for flow control
	 */
	template <class TYPE>
	class BlockQueue
	{
	private:
		/**
		 * @brief Internal node structure for element storage
		 */
		struct Node
		{
			TYPE data;	///< Storage for element data
			Node *prev; ///< Pointer to previous node in list
			Node *next; ///< Pointer to next node in list

			/**
			 * @brief Constructs an empty node
			 */
			Node() : prev(nullptr), next(nullptr) {}
		};

		// Memory management
		void *memoryBlock;			 ///< Raw memory block for node storage
		bool isInitialized;			 ///< Flag indicating successful initialization
		unsigned int maxSize;		 ///< Maximum capacity of the queue
		std::atomic<bool> isStopped; ///< Atomic flag for stopping all operations

		// List management
		Node *freeListHead; ///< Head of available nodes list
		Node *dataListHead; ///< Head of active data list
		Node *dataListTail; ///< Tail of active data list

		// Synchronization primitives
		std::mutex dataMutex;				  ///< Mutex protecting data list operations
		std::mutex freeListMutex;			  ///< Mutex protecting free list operations
		std::condition_variable notEmptyCond; ///< Signaled when data becomes available
		std::condition_variable notFullCond;  ///< Signaled when space becomes available

		/**
		 * @brief Allocates a single node from free list (no locking)
		 * @return Pointer to allocated node or nullptr if free list is empty
		 */
		Node *allocateNodeUnsafe()
		{
			if (UNLIKELY(!freeListHead))
				return nullptr;

			Node *node = freeListHead;
			freeListHead = node->next;
			if (LIKELY(freeListHead))
				freeListHead->prev = nullptr;

			return node;
		}

		/**
		 * @brief Allocates multiple contiguous nodes from free list (no locking)
		 * @param count Number of nodes to allocate
		 * @return Head node of allocated chain or nullptr if not enough nodes
		 */
		Node *allocateNodesUnsafe(unsigned int count)
		{
			if (UNLIKELY(!freeListHead || count == 0))
				return nullptr;

			Node *head = freeListHead;
			Node *tail = head;
			unsigned int allocated = 1;

			while (LIKELY(allocated < count && tail->next))
			{
				tail = tail->next;
				allocated++;
			}

			freeListHead = tail->next;
			if (LIKELY(freeListHead))
				freeListHead->prev = nullptr;

			tail->next = nullptr;
			return head;
		}

		/**
		 * @brief Recycles nodes back to free list (no locking)
		 * @param head First node in the chain to recycle
		 * @param tail Last node in the chain to recycle
		 */
		void recycleNodesUnsafe(Node *head, Node *tail)
		{
			if (UNLIKELY(!head || !tail))
				return;

			tail->next = freeListHead;
			if (LIKELY(freeListHead))
				freeListHead->prev = tail;

			freeListHead = head;
			head->prev = nullptr;
		}

	public:
		/**
		 * @brief Constructs an uninitialized queue
		 */
		BlockQueue() : memoryBlock(nullptr),
					   freeListHead(nullptr),
					   dataListHead(nullptr),
					   dataListTail(nullptr),
					   isInitialized(false),
					   isStopped(false),
					   maxSize(0) {}

		/**
		 * @brief Destroys queue and releases all resources
		 */
		~BlockQueue()
		{
			release();
		}

		/**
		 * @brief Initializes queue with fixed capacity
		 * @param capacity Maximum number of elements the queue can hold
		 * @return true if initialization succeeded, false otherwise
		 * @details Preallocates all required memory and builds initial free list
		 */
		bool init(unsigned int capacity)
		{
			if (UNLIKELY(isInitialized || capacity == 0))
				return false;

			memoryBlock = ::operator new(sizeof(Node) * capacity, std::nothrow);
			if (UNLIKELY(!memoryBlock))
				return false;

			Node *nodes = (Node *)(memoryBlock);
			freeListHead = &nodes[0];

			for (unsigned i = 0; i < capacity; ++i)
			{
				nodes[i].prev = (i == 0) ? nullptr : &nodes[i - 1];
				nodes[i].next = (i == capacity - 1) ? nullptr : &nodes[i + 1];
			}

			maxSize = capacity;
			isInitialized = true;
			return true;
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
			std::unique_lock<std::mutex> freeLock(freeListMutex);
			Node *node = allocateNodeUnsafe();
			freeLock.unlock();

			if (UNLIKELY(!node))
				return false;

			new (&node->data) TYPE(std::forward<T>(element));

			{
				std::lock_guard<std::mutex> dataLock(dataMutex);
				node->prev = dataListTail;
				node->next = nullptr;

				if (LIKELY(dataListTail))
					dataListTail->next = node;
				else
					dataListHead = node;
				dataListTail = node;
			}

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
		bool wait_push(T &&element)
		{
			Node *node = nullptr;
			{
				std::unique_lock<std::mutex> lock(freeListMutex);
				notFullCond.wait(lock, [this]
								 { return UNLIKELY(isStopped.load(std::memory_order_acquire)) || LIKELY(freeListHead != nullptr); });
				if (UNLIKELY(isStopped.load(std::memory_order_acquire)))
					return false;
				node = allocateNodeUnsafe();
			}

			new (&node->data) TYPE(std::forward<T>(element));

			{
				std::lock_guard<std::mutex> lock(dataMutex);
				node->prev = dataListTail;
				node->next = nullptr;

				if (LIKELY(dataListTail))
					dataListTail->next = node;
				else
					dataListHead = node;
				dataListTail = node;
			}

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
		bool wait_push(T &&element, const std::chrono::duration<Rep, Period> &timeout)
		{
			Node *node = nullptr;
			{
				std::unique_lock<std::mutex> lock(freeListMutex);
				if (!notFullCond.wait_for(lock, timeout, [this]
										  { return UNLIKELY(isStopped.load(std::memory_order_acquire)) || LIKELY(freeListHead != nullptr); }))
					return false;
				if (UNLIKELY(isStopped.load(std::memory_order_acquire)))
					return false;
				node = allocateNodeUnsafe();
			}

			new (&node->data) TYPE(std::forward<T>(element));

			{
				std::lock_guard<std::mutex> lock(dataMutex);
				node->prev = dataListTail;
				node->next = nullptr;

				if (LIKELY(dataListTail))
					dataListTail->next = node;
				else
					dataListHead = node;
				dataListTail = node;
			}

			notEmptyCond.notify_one();
			return true;
		}

		/**
		 * @brief Non-blocking element removal
		 * @param element Reference to store popped element
		 * @return true if element was retrieved, false if queue was empty
		 */
		bool pop(TYPE &element)
		{
			Node *node = nullptr;
			{
				std::unique_lock<std::mutex> lock(dataMutex);
				if (UNLIKELY(!dataListHead))
					return false;

				node = dataListHead;
				dataListHead = node->next;
				if (LIKELY(dataListHead))
					dataListHead->prev = nullptr;
				else
					dataListTail = nullptr;
			}

			element = std::move(node->data);
			node->data.~TYPE();

			{
				std::lock_guard<std::mutex> lock(freeListMutex);
				recycleNodesUnsafe(node, node);
			}

			notFullCond.notify_one();
			return true;
		}

		/**
		 * @brief Blocking element removal with indefinite wait
		 * @param element Reference to store popped element
		 * @return true if element was retrieved, false if queue was stopped
		 */
		bool wait_pop(TYPE &element)
		{
			Node *node = nullptr;
			{
				std::unique_lock<std::mutex> lock(dataMutex);
				notEmptyCond.wait(lock, [this]
								  { return UNLIKELY(isStopped.load(std::memory_order_acquire)) || LIKELY(dataListHead != nullptr); });
				if (UNLIKELY(!dataListHead))
					return false;

				node = dataListHead;
				dataListHead = node->next;
				if (LIKELY(dataListHead))
					dataListHead->prev = nullptr;
				else
					dataListTail = nullptr;
			}

			element = std::move(node->data);
			node->data.~TYPE();

			{
				std::lock_guard<std::mutex> lock(freeListMutex);
				recycleNodesUnsafe(node, node);
			}

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
		bool wait_pop(TYPE &element, const std::chrono::duration<Rep, Period> &timeout)
		{
			Node *node = nullptr;
			{
				std::unique_lock<std::mutex> lock(dataMutex);
				if (!notEmptyCond.wait_for(lock, timeout, [this]
										   { return UNLIKELY(isStopped.load(std::memory_order_acquire)) || LIKELY(dataListHead != nullptr); }))
					return false;
				if (UNLIKELY(!dataListHead))
					return false;

				node = dataListHead;
				dataListHead = node->next;
				if (LIKELY(dataListHead))
					dataListHead->prev = nullptr;
				else
					dataListTail = nullptr;
			}

			element = std::move(node->data);
			node->data.~TYPE();

			{
				std::lock_guard<std::mutex> lock(freeListMutex);
				recycleNodesUnsafe(node, node);
			}

			notFullCond.notify_one();
			return true;
		}

		/**
		 * @brief Bulk push for multiple elements
		 * @param elements Array of elements to push
		 * @param count Number of elements to push
		 * @return Actual number of elements pushed
		 */
		unsigned int pushBulk(const TYPE *elements, unsigned int count)
		{
			if (UNLIKELY(count == 0))
				return 0;

			std::unique_lock<std::mutex> freeLock(freeListMutex);
			Node *head = allocateNodesUnsafe(count);
			freeLock.unlock();

			if (UNLIKELY(!head))
				return 0;

			Node *current = head;
			Node *tail = nullptr;
			unsigned actualCount = 0;

			for (; LIKELY(actualCount < count && current); ++actualCount)
			{
				new (&current->data) TYPE(elements[actualCount]);
				tail = current;
				current = current->next;
			}

			{
				std::lock_guard<std::mutex> dataLock(dataMutex);
				if (LIKELY(dataListTail))
				{
					dataListTail->next = head;
					head->prev = dataListTail;
				}
				else
				{
					dataListHead = head;
					head->prev = nullptr;
				}
				dataListTail = tail;
			}

			notEmptyCond.notify_all();
			return actualCount;
		}

		/**
		 * @brief Blocking bulk push with indefinite wait
		 * @param elements Array of elements to push
		 * @param count Number of elements to push
		 * @return Actual number of elements pushed before stop
		 */
		unsigned int wait_pushBulk(const TYPE *elements, unsigned int count)
		{
			if (UNLIKELY(count == 0) || UNLIKELY(isStopped.load(std::memory_order_acquire)))
				return 0;

			unsigned int pushed = 0;

			while (LIKELY(pushed < count) && LIKELY(!isStopped.load(std::memory_order_acquire)))
			{
				Node *head = nullptr;
				unsigned allocated = 0;

				{
					std::unique_lock<std::mutex> lock(freeListMutex);
					notFullCond.wait(lock, [this]
									 { return UNLIKELY(isStopped.load(std::memory_order_acquire)) || LIKELY(freeListHead != nullptr); });
					if (UNLIKELY(isStopped.load(std::memory_order_acquire)))
						break;

					unsigned remaining = count - pushed;
					head = allocateNodesUnsafe(remaining);
					if (UNLIKELY(!head))
						continue;

					allocated = 1;
					Node *tail = head;
					while (LIKELY(tail->next && allocated < remaining))
					{
						tail = tail->next;
						allocated++;
					}
				}

				Node *current = head;
				for (unsigned i = 0; LIKELY(i < allocated); ++i)
				{
					new (&current->data) TYPE(elements[pushed + i]);
					current = current->next;
				}

				{
					std::lock_guard<std::mutex> dataLock(dataMutex);
					Node *tailNode = head;
					for (unsigned j = 1; LIKELY(j < allocated && tailNode->next); ++j)
						tailNode = tailNode->next;

					if (LIKELY(dataListTail))
					{
						dataListTail->next = head;
						head->prev = dataListTail;
						dataListTail = tailNode;
					}
					else
					{
						dataListHead = head;
						dataListTail = tailNode;
					}
				}

				pushed += allocated;
				notEmptyCond.notify_all();
			}

			return pushed;
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
		unsigned int wait_pushBulk(const TYPE *elements, unsigned int count, const std::chrono::duration<Rep, Period> &timeout)
		{
			if (UNLIKELY(count == 0))
				return 0;

			auto deadline = std::chrono::steady_clock::now() + timeout;
			unsigned int pushed = 0;

			while (LIKELY(pushed < count) && LIKELY(!isStopped.load(std::memory_order_acquire)))
			{
				Node *head = nullptr;
					unsigned allocated = 0;
				{
					std::unique_lock<std::mutex> lock(freeListMutex);
					if (!notFullCond.wait_until(lock, deadline, [this]
												{ return UNLIKELY(isStopped.load(std::memory_order_acquire)) || LIKELY(freeListHead != nullptr); }))
						break;
					if (UNLIKELY(isStopped.load(std::memory_order_acquire)))
						break;

					unsigned remaining = count - pushed;
					head = allocateNodesUnsafe(remaining);
					if (UNLIKELY(!head))
						continue;

					allocated = 1;
					Node *tail = head;
					while (LIKELY(tail->next && allocated < remaining))
					{
						tail = tail->next;
						allocated++;
					}
				}

				Node *current = head;
				for (unsigned i = 0; LIKELY(i < allocated); ++i)
				{
					new (&current->data) TYPE(elements[pushed + i]);
					current = current->next;
				}

				{
					std::lock_guard<std::mutex> dataLock(dataMutex);
					Node *tailNode = head;
					for (unsigned j = 1; LIKELY(j < allocated && tailNode->next); ++j)
						tailNode = tailNode->next;

					if (LIKELY(dataListTail))
					{
						dataListTail->next = head;
						head->prev = dataListTail;
						dataListTail = tailNode;
					}
					else
					{
						dataListHead = head;
						dataListTail = tailNode;
					}
				}

				pushed += allocated;
				notEmptyCond.notify_all();
			}
			return pushed;
		}

		/**
		 * @brief Bulk element retrieval
		 * @param elements Array to store retrieved elements
		 * @param count Maximum number of elements to retrieve
		 * @return Actual number of elements retrieved
		 */
		unsigned int popBulk(TYPE *elements, unsigned int count)
		{
			if (UNLIKELY(count == 0))
				return 0;

			Node *head = nullptr;
			Node *tail = nullptr;
			unsigned actualCount = 0;

			{
				std::unique_lock<std::mutex> lock(dataMutex);
				if (UNLIKELY(!dataListHead))
					return 0;

				head = dataListHead;
				tail = head;
				actualCount = 1;

				while (LIKELY(tail->next && actualCount < count))
				{
					tail = tail->next;
					actualCount++;
				}

				dataListHead = tail->next;
				if (LIKELY(dataListHead))
					dataListHead->prev = nullptr;
				else
					dataListTail = nullptr;
			}

			Node *current = head;
			for (unsigned i = 0; LIKELY(i < actualCount); ++i)
			{
				elements[i] = std::move(current->data);
				current->data.~TYPE();
				current = current->next;
			}

			{
				std::lock_guard<std::mutex> lock(freeListMutex);
				recycleNodesUnsafe(head, tail);
			}

			notFullCond.notify_all();
			return actualCount;
		}

		/**
		 * @brief Blocking bulk retrieval with indefinite wait (returns as soon as any elements are available)
		 * @param elements Array to store retrieved elements
		 * @param count Maximum number of elements to retrieve
		 * @return Actual number of elements retrieved before stop
		 * @details This version returns immediately after the first successful wait, getting as many elements
		 *          as are currently available (up to count), rather than waiting to fill the full count
		 */
		unsigned int wait_popBulk(TYPE *elements, unsigned int count)
		{
			if (UNLIKELY(count == 0) || UNLIKELY(isStopped.load(std::memory_order_acquire)))
				return 0;

			Node *head = nullptr;
			Node *tail = nullptr;
			unsigned int allocated = 0;

			{
				std::unique_lock<std::mutex> lock(dataMutex);
				notEmptyCond.wait(lock, [this]
								  { return UNLIKELY(isStopped.load(std::memory_order_acquire)) || LIKELY(dataListHead != nullptr); });

				if (UNLIKELY(!dataListHead))
					return 0;

				head = dataListHead;
				tail = head;
				allocated = 1;

				while (LIKELY(tail->next && allocated < count))
				{
					tail = tail->next;
					allocated++;
				}

				dataListHead = tail->next;
				if (LIKELY(dataListHead))
					dataListHead->prev = nullptr;
				else
					dataListTail = nullptr;
			}

			Node *current = head;
			for (unsigned i = 0; LIKELY(i < allocated); ++i)
			{
				elements[i] = std::move(current->data);
				current->data.~TYPE();
				current = current->next;
			}

			{
				std::lock_guard<std::mutex> lock(freeListMutex);
				recycleNodesUnsafe(head, tail);
			}

			notFullCond.notify_all();
			return allocated;
		}

		/**
		 * @brief Blocking bulk retrieval with timeout (returns as soon as any elements are available)
		 * @tparam Rep Chrono duration representation type
		 * @tparam Period Chrono duration period type
		 * @param elements Array to store retrieved elements
		 * @param count Maximum number of elements to retrieve
		 * @param timeout Maximum time to wait for data
		 * @return Actual number of elements retrieved
		 * @details This version returns immediately after the first successful wait, getting as many elements
		 *          as are currently available (up to count), rather than waiting to fill the full count
		 */
		template <class Rep, class Period>
		unsigned int wait_popBulk(TYPE *elements, unsigned int count, const std::chrono::duration<Rep, Period> &timeout)
		{
			if (UNLIKELY(count == 0))
				return 0;

			Node *head = nullptr;
			Node *tail = nullptr;
			unsigned int allocated = 0;

			{
				std::unique_lock<std::mutex> lock(dataMutex);
				if (!notEmptyCond.wait_for(lock, timeout, [this]
										   { return UNLIKELY(isStopped.load(std::memory_order_acquire)) || LIKELY(dataListHead != nullptr); }))
				{
					return 0;
				}

				if (UNLIKELY(!dataListHead))
					return 0;

				head = dataListHead;
				tail = head;
				allocated = 1;

				while (LIKELY(tail->next && allocated < count))
				{
					tail = tail->next;
					allocated++;
				}

				dataListHead = tail->next;
				if (LIKELY(dataListHead))
					dataListHead->prev = nullptr;
				else
					dataListTail = nullptr;
			}

			Node *current = head;
			for (unsigned i = 0; LIKELY(i < allocated); ++i)
			{
				elements[i] = std::move(current->data);
				current->data.~TYPE();
				current = current->next;
			}

			{
				std::lock_guard<std::mutex> lock(freeListMutex);
				recycleNodesUnsafe(head, tail);
			}

			notFullCond.notify_all();
			return allocated;
		}

		/**
		 * @brief Signals all waiting threads to stop blocking
		 * @details Sets the stop flag and notifies all condition variables
		 */
		void stopWait()
		{
			isStopped.store(true, std::memory_order_release);
			notEmptyCond.notify_all();
			notFullCond.notify_all();
		}

		/**
		 * @brief Releases all resources and resets queue state
		 * @details Destroys elements, frees memory, and resets to initial state
		 */
		void release()
		{
			stopWait();

			Node *current = dataListHead;
			while (current)
			{
				current->data.~TYPE();
				current = current->next;
			}

			if (LIKELY(memoryBlock))
			{
				::operator delete(memoryBlock);
				memoryBlock = nullptr;
			}
		}

		// Disable copying
		BlockQueue(const BlockQueue &) = delete;
		BlockQueue &operator=(const BlockQueue &) = delete;
	};
}
#endif // HSLL_BLOCKQUEUE