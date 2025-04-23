#ifndef HSLL_SPTYPES
#define HSLL_SPTYPES

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
    typedef void (*ExitProc)(void *ctx);
    ///< Recieve event callback type
    typedef void (*RecvProc)(void *ctx, const char *data, ssize_t size, const char *ip, unsigned short port);

    /**
     * @brief Protocol types for socket creation
     */
    enum PROTOCOL
    {
        PROTOCOL_UDP = SOCK_DGRAM, ///< UDP protocol (connectionless datagram)
        PROTOCOL_TCP = SOCK_STREAM ///< TCP protocol (connection-oriented stream)
    };

    /**
     * @brief Address family types for socket operations
     */
    enum ADDRESS_FAMILY
    {
        ADDRESS_FAMILY_INET = AF_INET,  ///< IPv4 address family
        ADDRESS_FAMILY_INET6 = AF_INET6 ///< IPv6 address family
    };

    /**
     * @brief Policy options when thread pool is full
     */
    enum FULL_LOAD_POLICY
    {
        FULL_LOAD_POLICY_WAIT,   ///< Block and wait until space becomes available in queue
        FULL_LOAD_POLICY_DISCARD ///< Silently discard new tasks when queue is full
    };

    /**
     * @brief Enumeration for log levels
     */
    enum LOG_LEVEL
    {
        LOG_LEVEL_INFO = 0,    // Informational messages
        LOG_LEVEL_WARNING = 1, // Warning messages
        LOG_LEVEL_CRUCIAL = 2, // Crucial messages
        LOG_LEVEL_ERROR = 3,   // Error messages
        LOG_LEVEL_NONE = 4,    // NONE
    };

    /**
     * @brief SPSockTcp configuration
     */
    struct SPConfig
    {
        ///< Read buffer size
        int READ_BSIZE;
        ///< Write buffer size
        int WRITE_BSIZE;
        ///< Maximum events processed per epoll cycle
        int MAX_EVENT_BSIZE;
        ///< Epoll wait timeout in milliseconds (-1 means infinite wait)
        int EPOLL_TIMEOUT_MILLISECONDS;
        ///< By default, epoll listens for events(EPOLLIN EPOLLOUT EPOLLIN|EPOLLOUT)
        int EPOLL_DEFAULT_EVENT;
        ///< Maximum number of tasks in thread pool queue
        int THREADPOOL_QUEUE_LENGTH;
        ///< Default number of threads when system core count cannot be determined
        int THREADPOOL_DEFAULT_THREADS_NUM;
        ///< Number of tasks to submit to thread pool in a single batch
        int THREADPOOL_BATCH_SIZE_SUBMIT;
        ///< Number of tasks for thread pool to process in a single batch
        int THREADPOOL_BATCH_SIZE_PROCESS;
        ///< Minimum log printing level
        LOG_LEVEL MIN_LOG_LEVEL;
    };
}

#endif