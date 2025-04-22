#ifndef HSLL_SPTYPES
#define HSLL_SPTYPES

namespace HSLL
{

/// Marks functions that should only be called once
#define SPSOCK_ONE_TIME_CALL
/// Read buffer size (16KB)
#define SPSOCK_READ_BSIZE 16 * 1024
/// Write buffer size (32KB)
#define SPSOCK_WRITE_BSIZE 32 * 1024
/// Maximum events processed per epoll cycle
#define SPSOCK_MAX_EVENT_BSIZE 5000
/// Epoll wait timeout in milliseconds (-1 means infinite wait)
#define SPSOCK_EPOLL_TIMEOUT_MILLISECONDS -1
/// By default, epoll listens for events(EPOLLIN EPOLLOUT EPOLLIN|EPOLLOUT)
#define SPSOCK_EPOLL_DEFAULT_EVENT EPOLLIN
/// Maximum number of tasks in thread pool queue
#define SPSOCK_THREADPOOL_QUEUE_LENGTH 10000
/// Default number of threads when system core count cannot be determined
#define SPSOCK_THREADPOOL_DEFAULT_THREADS_NUM 4
/// Number of tasks to submit to thread pool in a single batch
#define SPSOCK_THREADPOOL_BATCH_SIZE_SUBMIT 10
/// Number of tasks for thread pool to process in a single batch
#define SPSOCK_THREADPOOL_BATCH_SIZE_PROCESS 5

    // Compile-time assertions to validate configuration values
    static_assert(SPSOCK_READ_BSIZE > 0);
    static_assert(SPSOCK_WRITE_BSIZE > 0);
    static_assert(SPSOCK_THREADPOOL_QUEUE_LENGTH > 0);
    static_assert(SPSOCK_THREADPOOL_DEFAULT_THREADS_NUM > 0);
    static_assert(SPSOCK_THREADPOOL_BATCH_SIZE_SUBMIT > 0);
    static_assert(SPSOCK_THREADPOOL_BATCH_SIZE_PROCESS > 0);
    static_assert(SPSOCK_MAX_EVENT_BSIZE > 0);
    static_assert(SPSOCK_EPOLL_TIMEOUT_MILLISECONDS > 0 || SPSOCK_EPOLL_TIMEOUT_MILLISECONDS == -1);

    // Forward declaration of SOCKController class
    class SOCKController;

    /// Callback function type for read events
    typedef void (*ReadProc)(SOCKController *controller);
    /// Callback function type for write events
    typedef void (*WriteProc)(SOCKController *controller);
    /// Combined callback function type for read/write events
    typedef void (*ReadWriteProc)(SOCKController *controller);
    /// Task processing function type for thread pool
    typedef void (*TaskProc)(SOCKController *ctx, ReadWriteProc proc);
    /// Callback function type for connection close events
    typedef void (*CloseProc)(SOCKController *controller);
    /// Callback function type for event loop exit events
    typedef void (*ExitProc)(void *ctx);
    ///< Recieve event callback type
    typedef void (*RecvProc)(void *ctx, const char *data, ssize_t size, const char *ip, unsigned short port);
    ///< Connection callback type
    typedef void *(*ConnectProc)(const char *ip, unsigned short port);

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
     * @brief Structure holding network event callback functions
     */
    struct SPSockProc
    {
        ReadProc rdp;    ///< Callback for read events (data available to read)
        WriteProc wtp;   ///< Callback for write events (ready to send data)
        ConnectProc cnp; ///< Callback for new connections (TCP only)
        CloseProc csp;   ///< Callback for connection closure events
    };

    /**
     * @brief TCP keep-alive configuration structure
     */
    struct SPSockAlive
    {
        int keepAlive;      ///< Enable/disable keep-alive (0 = disable, 1 = enable)
        int aliveSeconds;   ///< Idle time (seconds) before sending keepalive probes
        int detectTimes;    ///< Number of unacknowledged probes before declaring dead
        int detectInterval; ///< Interval (seconds) between keepalive probes
    };

    /**
     * @brief Thread-safe connection close list
     *
     * Used to safely manage connection closures across multiple threads
     */
    struct CloseList
    {
        std::mutex mtx;             ///< Mutex for thread synchronization
        std::list<int> connections; ///< List of file descriptors to be closed
    };

    /**
     * @brief Array of error descriptions for SPSock operations
     *
     * @note Errors are ordered logically: generic errors first, then socket-specific,
     *       followed by epoll/signal errors, and finally state-related errors
     */
    constexpr const char *const SPSockErrors[] = {
        "No error occurred",                        // 0
        "Invalid parameter",                        // 1: Generic error for invalid inputs
        "socket() error",                           // 2: Socket creation errors
        "setsockopt() error",                       // 3: Socket option setting errors
        "bind() error",                             // 4: Address binding errors
        "listen() error",                           // 5: TCP listen errors
        "accept() error",                           // 6: TCP accept errors
        "recvfrom() error",                         // 7: UDP receive errors
        "sendto() error",                           // 8: UDP send errors
        "epoll_create1() error",                    // 9: Epoll instance creation errors
        "epoll_ctl() error",                        // 10: Epoll control operation errors
        "epoll_wait() error",                       // 11: Epoll wait errors
        "signal() error",                           // 12: Signal handling errors
        "Listen() not called",                      // 13: Invalid state - Listen() required
        "SetCallback() not called",                 // 14: Invalid state - Callbacks not set
        "Function cannot be called multiple times", // 15: Invalid state - One-time function called again
        "Bind() not called",                        // 16: Invalid state - Bind() required
        "ThreadPool init error",                    // 17: Thread pool initialization failed
        "open() error"                              // 18: File opening error
    };
}

#endif
