#ifndef HSLL_SPTYPES
#define HSLL_SPTYPES

#include <netinet/in.h>
#include <sys/types.h>

namespace HSLL
{

/// Marks functions that should only be called once
#define SPSOCK_ONE_TIME_CALL

    // Forward declaration of SOCKController class
    class SOCKController;

    /// Callback function type for read events
    typedef void (*ReadProc)(SOCKController *controller);
    /// Callback function type for write events
    typedef void (*WriteProc)(SOCKController *controller);
    /// Combined callback function type for read/write events
    typedef void (*ReadWriteProc)(SOCKController *controller);
    ///< Connection callback type
    typedef void *(*ConnectProc)(const char *ip, unsigned short port);
    /// Callback function type for connection close events
    typedef void (*CloseProc)(SOCKController *controller);
    /// Callback function type for event loop exit events
    typedef void (*RecvProc)(void *ctx, const char *data, ssize_t size, const char *ip, unsigned short port);

    /**
     * @brief Address family types for socket operations
     */
    enum ADDRESS_FAMILY
    {
        ADDRESS_FAMILY_INET = AF_INET,  ///< IPv4 address family
        ADDRESS_FAMILY_INET6 = AF_INET6 ///< IPv6 address family
    };

    /**
     * @brief Enumeration for log severity levels
     */
    enum LOG_LEVEL
    {
        LOG_LEVEL_INFO = 0,    ///< Informational messages (lowest severity)
        LOG_LEVEL_WARNING = 1, ///< Warning messages indicating potential issues
        LOG_LEVEL_CRUCIAL = 2, ///< Critical messages requiring immediate attention
        LOG_LEVEL_ERROR = 3,   ///< Error messages indicating failure conditions
    };

    /**
     * @brief Main socket configuration structure
     * @details Contains all tunable parameters for socket performance and behavior
     */
    struct SPConfig
    {
        ///< Read buffer size (must be multiple of 1024, minimum 1KB)
        int READ_BSIZE;

        ///< Write buffer size (must be multiple of 1024, minimum 1KB)
        int WRITE_BSIZE;

        ///< Number of blocks requested at a time by the buffer pool (range: 1-1024)
        int BUFFER_POOL_PEER_ALLOC_NUM;

        ///< Minimum number of blocks in the buffer pool (must ≥ BUFFER_POOL_PEER_ALLOC_NUM)
        int BUFFER_POOL_MIN_BLOCK_NUM;

        ///< Maximum events processed per epoll cycle (range: 1-65535)
        int EPOLL_MAX_EVENT_BSIZE;

        ///< Epoll wait timeout in milliseconds (-1: block indefinitely, 0: non-block, >0: timeout ms)
        int EPOLL_TIMEOUT_MILLISECONDS;

        ///< Default epoll events (valid combinations: EPOLLIN, EPOLLOUT, or EPOLLIN|EPOLLOUT)
        int EPOLL_DEFAULT_EVENT;

        ///< Maximum number of tasks in thread pool queue (range: 1-1048576)
        int THREADPOOL_QUEUE_LENGTH;

        ///< Batch size for submitting tasks to thread pool (must < THREADPOOL_QUEUE_LENGTH)
        int THREADPOOL_BATCH_SIZE_SUBMIT;

        ///< Batch size for processing tasks in thread pool (range: 1-1024)
        int THREADPOOL_BATCH_SIZE_PROCESS;

        ///< Weight ratio for worker threads vs IO threads (range: 0.0 < ratio < 1.0)
        float WORKER_THREAD_RATIO;

        ///< Minimum log printing level (valid LOG_LEVEL enum values)
        LOG_LEVEL MIN_LOG_LEVEL;
    };
}

#endif