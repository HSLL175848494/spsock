#ifndef HSLL_SPBUFFERPOOL
#define HSLL_SPBUFFERPOOL

#include "SPTypes.h"
#include "noncopyable.h"

using namespace HSLL::DEFER;

namespace HSLL
{
    /**
     * @brief Singleton memory pool manager for efficient buffer allocation
     * Manages separate memory pools for read/write buffers using a linked list structure.
     * Implements reference counting for block management and memory reuse.
     */
    class SPBufferPool : noncopyable
    {
        static SPBufferPool pool; ///< Singleton instance

        void *rBuffers;      ///< Head of free list for read buffers
        void *wBuffers;      ///< Head of free list for write buffers
        unsigned int rbSize; ///< Number of available read buffers in pool
        unsigned int wbSize; ///< Number of available write buffers in pool

        /**
         * @brief Attempt to allocate a block of buffers
         * @param num Number of buffer pairs (read+write) to allocate
         * @return true if allocation succeeded, false otherwise
         * Allocates a memory block containing 'num' read/write buffer pairs.
         * Each buffer is preceded by metadata (block reference + next pointer).
         * Organized as:
         * [header][read buffers][write buffers]
         */
        bool tryAlloc(int num)
        {
            const int peerReadBSize = configGlobal.READ_BSIZE + 2 * sizeof(void *);
            const int peerWriteBSize = configGlobal.WRITE_BSIZE + 2 * sizeof(void *);
            const int totalSize = 4 + (peerReadBSize + peerWriteBSize) * num;

            void *buf;
            if ((buf = malloc(totalSize)) == nullptr)
                return false;

            *(unsigned int *)(buf) = num * 2;
            void *pReadBuf = (char *)(buf) + 4;
            for (int i = 0; i < num; ++i)
            {
                void *current = (char *)(pReadBuf) + i * peerReadBSize;
                void **node = (void **)(current);
                node[0] = buf;
                node[1] = (i < num - 1) ? (char *)(current) + peerReadBSize : rBuffers;
            }
            rBuffers = pReadBuf;
            rbSize += num;

            void *pWriteBuf = (char *)(pReadBuf) + peerReadBSize * num;
            for (int i = 0; i < num; ++i)
            {
                void *current = (char *)(pWriteBuf) + i * peerWriteBSize;
                void **node = (void **)(current);
                node[0] = buf;
                node[1] = (i < num - 1) ? (char *)(current) + peerWriteBSize : wBuffers;
            }
            wBuffers = pWriteBuf;
            wbSize += num;

            return true;
        }

        /**
         * @brief Allocate buffers using exponential backoff strategy
         * @return true if allocation succeeded, false on complete failure
         */
        bool Alloc()
        {
            int num = configGlobal.BUFFER_POOL_PEER_ALLOC_NUM;
            while (num > 0)
            {
                if (tryAlloc(num))
                    return true;
                num /= 2;
            }
            return false;
        }

        /**
         * @brief Internal method to get a read buffer from pool
         * @return Pointer to allocated buffer or NULL on failure
         */
        void *GetReadBufferInner()
        {
            if (!rBuffers && !Alloc())
                return NULL;

            void *node = rBuffers;
            void **nodePtr = (void **)(node);
            rBuffers = nodePtr[1];
            --rbSize;

            return (char *)(node) + 2 * sizeof(void *);
        }

        /**
         * @brief Internal method to get a write buffer from pool
         * @return Pointer to allocated buffer or NULL on failure
         */
        void *GetWriteBufferInner()
        {
            if (!wBuffers && !Alloc())
                return NULL;

            void *node = wBuffers;
            void **nodePtr = (void **)(node);
            wBuffers = nodePtr[1];
            --wbSize;

            return (char *)(node) + 2 * sizeof(void *);
        }

        /**
         * @brief Internal method to release read buffer back to pool
         * @param buf Buffer to release (must be pointer returned by GetReadBufferInner)
         * Either adds buffer back to free list or decrements block reference count
         */
        void FreeReadBuffferInner(void *buf)
        {
            void *node = (char *)(buf)-2 * sizeof(void *);
            void **nodePtr = (void **)(node);
            unsigned int &refCount = *(unsigned int *)(nodePtr[0]);

            if (rbSize >= configGlobal.BUFFER_POOL_MIN_BLOCK_NUM)
            {
                --refCount;
                if (refCount == 0)
                    free(nodePtr[0]);
            }
            else
            {
                nodePtr[1] = rBuffers;
                rBuffers = node;
                ++rbSize;
            }
        }

        /**
         * @brief Internal method to release write buffer back to pool
         * @param buf Buffer to release (must be pointer returned by GetWriteBufferInner)
         * Either adds buffer back to free list or decrements block reference count
         */
        void FreeWriteBufferInner(void *buf)
        {
            void *node = (char *)(buf)-2 * sizeof(void *);
            void **nodePtr = (void **)(node);
            unsigned int &refCount = *(unsigned int *)(nodePtr[0]);

            if (wbSize >= configGlobal.BUFFER_POOL_MIN_BLOCK_NUM)
            {
                --refCount;
                if (refCount == 0)
                    free(nodePtr[0]);
            }
            else
            {
                nodePtr[1] = wBuffers;
                wBuffers = node;
                ++wbSize;
            }
        }

        /**
         * @brief Releases all memory blocks by traversing free lists
         * Decrements reference counts and frees blocks when count reaches zero
         */
        void releaseAllBlocks()
        {
            while (rBuffers)
            {
                void **node = (void **)(rBuffers);
                unsigned int &refCount = *(unsigned int *)(node[0]);
                rBuffers = node[1];
                --refCount;
                if (refCount == 0)
                    free(node[0]);
            }

            while (wBuffers)
            {
                void **node = (void **)(wBuffers);
                unsigned int &refCount = *(unsigned int *)(node[0]);
                wBuffers = node[1];
                --refCount;
                if (refCount == 0)
                    free(node[0]);
            }
        }

        /**
         * @brief Private constructor initializes empty pools
         */
        SPBufferPool() : rbSize(0), wbSize(0), rBuffers(NULL), wBuffers(NULL) {}

        /**
         * @brief Destructor cleans up all allocated memory blocks
         */
        ~SPBufferPool()
        {
            releaseAllBlocks();
        }

    public:
        /**
         * @brief Get a buffer from the appropriate pool
         * @param type Buffer type (read/write)
         * @return Pointer to allocated buffer or NULL on failure
         */
        static void *GetBuffer(BUFFER_TYPE type)
        {
            return (type == BUFFER_TYPE_READ) ? pool.GetReadBufferInner() : pool.GetWriteBufferInner();
        }

        /**
         * @brief Return a buffer to its pool
         * @param buf Buffer to release
         * @param type Buffer type (read/write)
         */
        static void FreeBuffer(void *buf, BUFFER_TYPE type)
        {
            (type == BUFFER_TYPE_READ) ? pool.FreeReadBuffferInner(buf) : pool.FreeWriteBufferInner(buf);
        }

        /**
         * @brief Resets the buffer pool to initial state, releasing all allocated memory
         * @note User must ensure all buffers are returned before calling this method.
         *       Calling this with outstanding buffers will cause memory corruption.
         */
        static void reset()
        {
            pool.releaseAllBlocks();
            pool.rbSize = 0;
            pool.wbSize = 0;
            pool.rBuffers = nullptr;
            pool.wBuffers = nullptr;
        }
    };
}

#endif