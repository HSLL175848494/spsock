#ifndef HSLL_SPTYPES
#define HSLL_SPTYPES

namespace HSLL
{
    /**
     * @brief Macro to delete class copy/move constructors and assignment operators
     */
#define SPSOCK_CONSTRUCTOR_DELETE(cName)      \
    cName(const cName &) = delete;            \
    cName(cName &&) = delete;                 \
    cName &operator=(const cName &) = delete; \
    cName &operator=(cName &&) = delete;

#define SPSOCK_ONE_TIME_CALL                    ///< Marks functions that should only be called once
#define SPSOCK_MAX_EVENT_BSIZE 5000             ///< Maximum events processed per epoll cycle
#define SPSOCK_EPOLL_TIMEOUT_MILLISECONDS -1    ///< Epoll wait timeout in milliseconds,default infinite
#define SPSOCK_THREADPOOL_QUEUE_LENGTH 10000    ///< The maximum number of task queue
#define SPSOCK_THREADPOOL_DEFAULT_THREADS_NUM 4 ///< If the number of system cores fails to be obtained, set the default number of threads
#define SPSOCK_THREADPOOL_BATCH_SIZE_SUBMIT 10  ///< Number of tasks to submit in batch
#define SPSOCK_THREADPOOL_BATCH_SIZE_PROCESS 5  ///< Number of tasks to process in batch

    static_assert(SPSOCK_THREADPOOL_QUEUE_LENGTH > 0);
    static_assert(SPSOCK_THREADPOOL_DEFAULT_THREADS_NUM > 0);
    static_assert(SPSOCK_THREADPOOL_BATCH_SIZE_SUBMIT > 0);
    static_assert(SPSOCK_THREADPOOL_BATCH_SIZE_PROCESS > 0);
    static_assert(SPSOCK_MAX_EVENT_BSIZE > 0);
    static_assert(SPSOCK_EPOLL_TIMEOUT_MILLISECONDS > 0 || SPSOCK_EPOLL_TIMEOUT_MILLISECONDS == -1);

    typedef void (*ReadProc)(void *ctx);  ///< Read event callback type
    typedef void (*WriteProc)(void *ctx); ///< Write event callback type
    typedef void (*RWProc)(void *ctx);    ///< Read/Write event callback type
    typedef void (*CloseProc)(void *ctx); ///< Connection close callback type
    typedef void (*ExitProc)(void *ctx);  ///< Event loop Exit callback type

    class SOCKController;

    /**
     * @brief Connection callback type
     * @return Context pointer for connection-specific data
     */
    typedef void *(*ConnectProc)(SOCKController &controller, const char *ip, unsigned short port);

    /**
     * @brief Protocol types for socket creation
     */
    enum PROTOCOL
    {
        PROTOCOL_UDP = SOCK_DGRAM, ///< UDP protocol
        PROTOCOL_TCP = SOCK_STREAM ///< TCP protocol
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
        FULL_LOAD_POLICY_WAIT,   ///< Wait until task can be added to queue
        FULL_LOAD_POLICY_DISCARD ///< Discard task when queue is full
    };

    /**
     * @brief Structure holding callback functions
     */
    struct SPSockProc
    {
        ReadProc rdp;    ///< Read event handler
        WriteProc wtp;   ///< Write event handler
        ConnectProc cnp; ///< New connection handler
        CloseProc csp;   ///< Close event handler
    };

    /**
     * @brief Keep-alive configuration structure
     */
    struct SPSockAlive
    {
        int keepAlive;      ///< Enable keep-alive (0 = disable, 1 = enable)
        int aliveSeconds;   ///< Idle time before probes
        int detectTimes;    ///< Number of probes
        int detectInterval; ///< Interval between probes
    };

    /**
     * @brief Connection information container
     */
    struct ConnectionInfo
    {
        void *ctx;        ///< User-defined context
        std::string info; ///< Connection identifier string
    };

    /**
     * @brief Thread-safe connection close list
     */
    struct CloseList
    {
        std::mutex mtx;             ///< Mutex for thread safety
        std::list<int> connections; ///< List of connections to close
    };

    /**
     * @brief Array of error descriptions for SPSock operations
     * @note Reordered for logic: generic errors first, then socket-specific, then epoll/signal, finally state errors
     */
    constexpr const char *const SPSockErrors[] = {
        "No error occurred",                        // 0
        "Invalid parameter",                        // 1: Generic error for invalid inputs
        "socket() error",                           // 2: Socket creation errors
        "setsockopt() error",                       // 3
        "bind() error",                             // 4
        "listen() error",                           // 5
        "accept() error",                           // 6: Added for accept() failures
        "recvfrom() error",                         // 7: UDP-specific
        "sendto() error",                           // 8: UDP-specific
        "epoll_create1() error",                    // 9: Epoll-related errors
        "epoll_ctl() error",                        // 10
        "epoll_wait() error",                       // 11
        "signal() error",                           // 12: Signal handling error
        "Listen() not called",                      // 13: State-related errors
        "SetCallback() not called",                 // 14
        "Function cannot be called multiple times", // 15
        "Bind() not called"                         // 16
        "ThreadPool init error"                     // 17
    };
}

#endif