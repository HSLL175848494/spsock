#ifndef HSLL_SPSOCK
#define HSLL_SPSOCK

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <netinet/tcp.h>
#include <unordered_map>

#include "SPLog.hpp"
#include "SPDeferred.h"

namespace HSLL
{
    /**
     * @brief Template structure for socket address initialization
     * @tparam address_family IP version specification (IPv4/IPv6)
     */
    template <ADDRESS_FAMILY address_family>
    struct SOCKADDR_IN;

    /**
     * @brief Specialization for IPv4 address family
     */
    template <>
    struct SOCKADDR_IN<ADDRESS_FAMILY_INET>
    {
        using TYPE = sockaddr_in; ///< Underlying socket address type

        static unsigned int UDP_MAX_BSIZE; ///< Maximum UDP datagram size including headers

        /**
         * @brief Initializes IPv4 socket address structure
         * @param address Reference to sockaddr_in structure to initialize
         * @param port Host byte order port number to convert
         */
        static void INIT(sockaddr_in &address, unsigned short port);
    };

    /**
     * @brief Specialization for IPv6 address family
     */
    template <>
    struct SOCKADDR_IN<ADDRESS_FAMILY_INET6>
    {
        using TYPE = sockaddr_in6; ///< Underlying socket address type

        static unsigned int UDP_MAX_BSIZE; ///< Maximum UDP datagram size including headers

        /**
         * @brief Initializes IPv6 socket address structure
         * @param address Reference to sockaddr_in6 structure to initialize
         * @param port Host byte order port number to convert
         */
        static void INIT(sockaddr_in6 &address, unsigned short port);
    };

    /**
     * @brief TCP socket manager with event-driven architecture
     * @tparam address_family IP version (IPv4/IPv6)
     */
    template <ADDRESS_FAMILY address_family>
    class SPSockTcp : noncopyable
    {
    private:
        int status;   ///< Internal state flags
        int listenfd; ///< Listening socket descriptor

        linger lin;                                          ///< Linger options configuration
        SPSockProc proc;                                     ///< User-defined callback functions
        SPSockAlive alive;                                   ///< Keep-alive parameters
        CloseList cList;                                     ///< Connections pending closure
        std::vector<IOThreadInfo> loopInfo;                  ///< IO thread metadata
        std::vector<std::thread> loops;                      ///< IO event loop threads
        std::unordered_map<int, SOCKController> connections; ///< Active connections map

        static std::atomic<bool> exitFlag;          ///< Event loop termination control
        static SPSockTcp<address_family> *instance; ///< Singleton instance pointer

        /**
         * @brief Configures SO_LINGER socket option
         * @param fd Socket descriptor to configure
         */
        void SetLinger(int fd);

        /**
         * @brief Configures TCP keep-alive parameters
         * @param fd Socket descriptor to configure
         */
        void SetKeepAlive(int fd);

        /**
         * @brief Initiates controlled connection closure
         * @param controller Connection controller to schedule for closure
         * @note Adds connection to thread-safe closure list
         */
        static void ActiveClose(SOCKController *controller);

        /**
         * @brief Modifies epoll event subscriptions
         * @param controller Connection controller to modify
         * @param read Enable read events (EPOLLIN)
         * @param write Enable write events (EPOLLOUT)
         * @return true if epoll_ctl succeeded, false otherwise
         */
        static bool EnableEvent(SOCKController *controller, bool read, bool write);

        /**
         * @brief Signal handler for graceful shutdown
         * @param sg Received signal number
         */
        static void HandleExit(int sg);

        /**
         * @brief Accepts new connections and initializes controllers
         * @param idlefd Reserved file descriptor for EMFILE handling
         * @return false if error ocurred, true otherwise
         */
        bool HandleConnect(int idlefd);

        /**
         * @brief Processes connections in close list
         * @note Performs batch closure and resource cleanup
         */
        void HandleCloseList();

        /**
         * @brief Processes read-ready events
         * @param controller Connection controller with pending data
         * @param utilTask Task dispatcher for worker threads
         * @return false if connection should be closed, true otherwise
         */
        bool HandleRead(SOCKController *controller, UtilTask *utilTask);

        /**
         * @brief Processes write-ready events
         * @param controller Connection controller with write capacity
         * @param utilTask Task dispatcher for worker threads
         * @return false if connection should be closed, true otherwise
         */
        bool HandleWrite(SOCKController *controller, UtilTask *utilTask);

        /**
         * @brief Closes connection and cleans resources
         * @param controller Connection controller to destroy
         */
        void CloseConnection(SOCKController *controller);

        /**
         * @brief Calculates optimal IO/worker thread distribution
         * @param ioThreads Receives calculated IO thread count
         * @param workerThreads Receives worker thread count
         * @return true if calculation succeeded, false on error
         */
        bool CalculateOptimalThreadCounts(int *ioThreads, int *workerThreads);

        /**
         * @brief Initializes epoll instances and IO threads
         * @param pool Worker thread pool reference
         * @param num Number of IO threads to create
         * @return true if all resources initialized successfully
         */
        bool CreateIOEventLoop(ThreadPool<SockTaskTcp> *pool, int num);

        /**
         * @brief Make the io thread exit and close the corresponding file descriptor
         * @return true if exited normally
         */
        void ExitIOEventLoop();

        /**
         * @brief Main acceptor thread event loop
         * @note Manages listen socket and connection acceptance
         * @return true if exited normally
         */
        bool MainEventLoop();

        /**
         * @brief IO worker thread event processing loop
         * @param pool Worker thread pool reference
         * @param epollfd Epoll instance descriptor
         * @param exitfd Eventfd descriptor for shutdown signaling
         */
        void IOEventLoop(ThreadPool<SockTaskTcp> *pool, int epollfd, int exitfd);

        /**
         * @brief Releases all network resources
         * @note Closes sockets and clears connection maps
         */
        void Cleanup();

        /**
         * @brief Private constructor for singleton pattern
         */
        SPSockTcp();

        /**
         * @brief Private destructor for singleton pattern
         */
        ~SPSockTcp();

    public:
        /**
         * @brief Configures global runtime parameters
         * @param config Configuration structure with tuning parameters
         * @note Must be called before instance creation
         */
        static void Config(SPConfig config = {16 * 1024, 32 * 1024, 16, 64, 5000, EPOLLIN, 10000, 10, 5, 0.6, LOG_LEVEL_WARNING});

        /**
         * @brief Gets singleton instance reference
         * @return Pointer to singleton instance
         * @note Initializes instance on first call
         */
        static SPSockTcp *GetInstance();

        /**
         * @brief Starts listening on specified port
         * @param port Network port to bind
         * @return true if listen succeeded, false on error
         * @note One-time call during initialization
         */
        bool Listen(unsigned short port) SPSOCK_ONE_TIME_CALL;

        /**
         * @brief Enters main event processing loop
         * @return true if loop completed normally, false on error
         * @note Blocks until shutdown signal received
         */
        bool EventLoop() SPSOCK_ONE_TIME_CALL;

        /**
         * @brief Configures socket linger options
         * @param enable Enable/disable lingering
         * @param waitSeconds Timeout for pending data send
         * @return true if configuration succeeded
         */
        bool EnableLinger(bool enable, int waitSeconds = 5);

        /**
         * @brief Configures TCP keep-alive parameters
         * @param enable Enable/disable keep-alive
         * @param aliveSeconds Idle time before probes
         * @param detectTimes Unacknowledged probe limit
         * @param detectInterval Seconds between probes
         * @return true if configuration succeeded
         */
        bool EnableKeepAlive(bool enable, int aliveSeconds = 120, int detectTimes = 3, int detectInterval = 10);

        /**
         * @brief Registers user-defined event callbacks
         * @param cnp New connection callback (optional)
         * @param csp Connection close callback (optional)
         * @param rdp Data receive callback (optional)
         * @param wtp Write ready callback (optional)
         * @return false if all null, true otherwise
         */
        bool SetCallback(ConnectProc cnp = nullptr, CloseProc csp = nullptr, ReadProc rdp = nullptr, WriteProc wtp = nullptr);

        /**
         * @brief Sets buffer thresholds for event triggering
         * @param readMark Minimum bytes to trigger read callback
         * @param writeMark Maximum buffered bytes to trigger write callback
         */
        void SetWaterMark(unsigned int readMark = 0, unsigned int writeMark = 0xffffffff);

        /**
         * @brief Registers signal handler for graceful shutdown
         * @param sg Signal number to handle (e.g., SIGINT)
         * @return true if signal handler registered
         */
        bool SetSignalExit(int sg) SPSOCK_ONE_TIME_CALL;

        /**
         * @brief Signals event loops to terminate
         * @note Non-blocking call to initiate shutdown
         */
        static void SetExitFlag();

        /**
         * @brief Releases singleton resources
         * @note Closes sockets and deletes instance
         */
        static void Release();
    };

    /**
     * @brief UDP socket manager with event-driven architecture
     * @tparam address_family IP version (IPv4/IPv6)
     */
    template <ADDRESS_FAMILY address_family>
    class SPSockUdp : noncopyable
    {
    private:
        int sockfd;          ///< Bound socket descriptor
        unsigned int status; ///< Internal state flags

        void *ctx;    ///< User context for receive callback
        RecvProc rcp; ///< Datagram receive callback

        static std::atomic<bool> exitFlag;          ///< Event loop control
        static SPSockUdp<address_family> *instance; ///< Singleton instance

        /**
         * @brief Signal handler for shutdown requests
         * @param sg Received signal number
         */
        static void HandleExit(int sg);

        /**
         * @brief Private constructor for singleton pattern
         */
        SPSockUdp();

        /**
         * @brief Private destructor for singleton pattern
         */
        ~SPSockUdp();

    public:
        /**
         * @brief Configures minimum logging severity
         * @param minlevel Minimum log level to output
         */
        static void Config(LOG_LEVEL minlevel = LOG_LEVEL_WARNING);

        /**
         * @brief Gets singleton instance reference
         * @return Pointer to singleton instance
         */
        static SPSockUdp *GetInstance();

        /**
         * @brief Binds socket to network port
         * @param port Network port to bind
         * @return true if bind succeeded, false on error
         * @note One-time call during initialization
         */
        bool Bind(unsigned short port) SPSOCK_ONE_TIME_CALL;

        /**
         * @brief Enters datagram processing loop
         * @return true if loop completed normally
         * @note Blocks until shutdown signal received
         */
        bool EventLoop() SPSOCK_ONE_TIME_CALL;

        /**
         * @brief Sends datagram to specified endpoint
         * @param data Buffer containing payload
         * @param size Payload size in bytes
         * @param ip Destination IP address
         * @param port Destination port number
         * @return true if send succeeded
         */
        bool SendTo(const void *data, size_t size, const char *ip, unsigned short port);

        /**
         * @brief Registers signal handler for shutdown
         * @param sg Signal number to handle
         * @return true if handler registered successfully
         */
        bool SetSignalExit(int sg);

        /**
         * @brief Registers receive callback
         * @param rcp Datagram receive handler
         * @param ctx User context pointer (optional)
         * @return false if null callback, true otherwise
         */
        bool SetCallback(RecvProc rcp, void *ctx = nullptr);

        /**
         * @brief Signals event loop to terminate
         */
        static void SetExitFlag();

        /**
         * @brief Releases singleton resources
         * @note Closes socket and deletes instance
         */
        static void Release();
    };
}

#endif