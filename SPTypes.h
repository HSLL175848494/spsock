#ifndef HSLL_SPTYPES
#define HSLL_SPTYPES

#include <list>
#include <mutex>
#include <string.h>
#include <thread>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unordered_set>

namespace HSLL
{
/**
 * @brief Macro marking functions that should only be called once
 * @details Functions decorated with this should maintain their own state
 *          to prevent multiple executions.
 */
#define SPSOCK_ONE_TIME_CALL

    // Forward declaration
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
    ///< Recieve event callback type
    typedef void (*RecvProc)(void *ctx, int fd, const char *data, size_t size, const char *ip, unsigned short port);
    /// Task processing function type for thread pool
    typedef void (*TaskProc)(SOCKController *ctx, ReadWriteProc proc);
    ///< Re-listen for the event function pointer
    typedef void (*REnableProc)(SOCKController *controller);
    ///< File descriptor close callback function type
    typedef void (*FuncClose)(SOCKController *controller);
    ///< Socket event control function type
    typedef bool (*FuncEvent)(SOCKController *, bool read, bool write);

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
     * @brief Enumeration for log severity levels
     */
    enum LOG_LEVEL
    {
        LOG_LEVEL_INFO = 0,    ///< Informational messages (lowest severity)
        LOG_LEVEL_WARNING = 1, ///< Warning messages indicating potential issues
        LOG_LEVEL_CRUCIAL = 2, ///< Critical messages requiring immediate attention
        LOG_LEVEL_ERROR = 3,   ///< Error messages indicating failure conditions
        LOG_LEVEL_NONE = 10,   ///< No messages
    };

    /**
     * @brief Enumeration for buffer operation types
     */
    enum BUFFER_TYPE
    {
        BUFFER_TYPE_READ,  ///< Buffer used for incoming data operations
        BUFFER_TYPE_WRITE, ///< Buffer used for outgoing data operations
    };

    /**
     * @brief Structure defining watermark thresholds for flow control
     * @details Used to manage event triggering in network I/O operations based on buffer occupancy levels.
     * @note Read events are triggered when the read buffer contains at least readMark bytes of data.
     * Write events are triggered when pending data in the write buffer falls below or equals writeMark.
     */
    struct SPWaterMark
    {
        unsigned int readMark;  ///< High watermark for read buffer (triggers when data >= this value)
        unsigned int writeMark; ///< Low watermark for write buffer (triggers when pending data <= this value)
    };

    /**
     * @brief Structure holding network event callback functions
     */
    struct SPSockProc
    {
        ReadProc rdp;    ///< Callback for read events (data available to read)
        WriteProc wtp;   ///< Callback for write events (ready to send data)
        ConnectProc cnp; ///< Callback for new connections
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
     * @brief Structure containing information for I/O event loop threads
     */
    struct IOThreadInfo
    {
        int count;   ///< Number of active connections being handled by this thread
        int epollfd; ///< File descriptor for the epoll instance monitoring connections
        int exitfd;  ///< Event file descriptor used for thread termination signaling
    };

    /**
     * @brief Thread-safe connection close list
     * @details Used to safely manage connection closures across multiple threads
     */
    struct CloseList
    {
        std::mutex mtx;
        std::list<SOCKController *> connections;
    };

    /**
     * @brief Main socket configuration structure
     * @details Contains all tunable parameters for socket performance and behavior
     */
    struct SPTcpConfig
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

        ///< Default epoll events (valid combinations: EPOLLIN, EPOLLOUT, or EPOLLIN|EPOLLOUT)
        int EPOLL_DEFAULT_EVENT;

        ///< Maximum number of tasks in thread pool queue (range: 1-1048576)
        int THREADPOOL_QUEUE_LENGTH;

        ///< Batch size for submitting tasks to thread pool (must < THREADPOOL_QUEUE_LENGTH)
        int THREADPOOL_BATCH_SIZE_SUBMIT;

        ///< Batch size for processing tasks in thread pool (range: 1-1024)
        int THREADPOOL_BATCH_SIZE_PROCESS;

        ///< Weight ratio for worker threads (range: 0.0 < ratio < 1.0)
        float WORKER_THREAD_RATIO;

        ///< Minimum log printing level (valid LOG_LEVEL enum values)
        LOG_LEVEL MIN_LOG_LEVEL;
    };

    /**
     * @brief Main socket configuration structure
     * @details Contains all tunable parameters for socket performance and behavior
     */
    struct SPUdpConfig
    {
        // Size of socket receive buffer (must be multiple of 1024, minimum 200KB)
        int RECV_BSIZE;

        ///< Maximum expected UDP payload size (effective data excluding headers)(range: 1452-65507)
        int MAX_PAYLOAD_SIZE;

        ///< Minimum log printing level (valid LOG_LEVEL enum values)
        LOG_LEVEL MIN_LOG_LEVEL;
    };

    /**
     * @brief Namespace for deferred or lazy initialization components
     * @details Contains objects requiring delayed initialization or global state
     */
    namespace DEFER
    {
        class SPDefered;
        extern SPTcpConfig tcpConfig;
        extern SPUdpConfig udpConfig;
        extern SPWaterMark markGlobal;
        extern REnableProc renableProc;
        extern FuncClose funcClose;
        extern FuncEvent funcEvent;
        extern LOG_LEVEL minLevel;
    }
}

#endif