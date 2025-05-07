#ifndef HSLL_SPSOCK
#define HSLL_SPSOCK

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
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

        static unsigned int UDP_MAX_BSIZE; ///< Max buffer size for UDP

        /**
         * @brief Initializes IPv4 socket address structure
         * @param address Reference to sockaddr_in structure
         * @param port Network byte order port number
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

        static unsigned int UDP_MAX_BSIZE; ///< Max buffer size for UDP

        /**
         * @brief Initializes IPv6 socket address structure
         * @param address Reference to sockaddr_in6 structure
         * @param port Network byte order port number
         */
        static void INIT(sockaddr_in6 &address, unsigned short port);
    };

    /**
     * @brief Main TCP socket management class
     * @tparam address_family IP version (IPv4/IPv6)
     */
    template <ADDRESS_FAMILY address_family>
    class SPSockTcp : noncopyable
    {
    private:
        linger lin;                                          ///< Linger configuration
        SPSockProc proc;                                     ///< User connections callbacks
        SPSockAlive alive;                                   ///< Keep-alive settings
        CloseList cList;                                     ///< Connection close list
        std::unordered_map<int, SOCKController> connections; ///< Active connections

        int status;   ///< Internal status flags
        int idlefd;   ///< Idle file descriptor
        int epollfd;  ///< Epoll file descriptor
        int listenfd; ///< Listening socket descriptor

        static void *exitCtx;                       ///< Event loop exit context
        static ExitProc exitProc;                   ///< Event loop exit procedure
        static std::atomic<bool> exitFlag;          ///< Event loop control flag
        static SPSockTcp<address_family> *instance; ///< Singleton instance

        /**
         * @brief Configures socket linger options
         * @param fd Socket descriptor to configure
         */
        void SetLinger(int fd);

        /**
         * @brief Configures TCP keep-alive options
         * @param fd Socket descriptor to configure
         */
        void SetKeepAlive(int fd);

        /**
         * @brief Actively initiates closure of a connection by adding it to the close list
         * @param controller Pointer to the connection controller to be closed
         * @note This is a static member function that operates on the singleton instance
         */
        static void ActiveClose(SOCKController *controller);

        /**
         * @brief Enables or modifies epoll event monitoring for a file descriptor
         * @param fd File descriptor to modify
         * @param read Whether to enable read events (EPOLLIN)
         * @param write Whether to enable write events (EPOLLOUT)
         * @return true if event modification succeeded, false otherwise
         */
        static bool EnableEvent(int fd, bool read, bool write);

        /**
         * @brief Signal handler for graceful shutdown
         * @param sg Signal number received
         */
        static void HandleExit(int sg);

        /**
         * @brief Prepares the epoll instance and thread pool for event processing
         * @param events Pre-allocated epoll event buffer
         * @param pool Pointer to the thread pool instance
         * @return true if preparation succeeded, false otherwise
         * @note Initializes epoll instance, configures listening socket,
         *       and sets up worker threads
         */
        bool Prepare(epoll_event *events, ThreadPool<SockTask> *pool);

        /**
         * @brief Handles new incoming connections
         */
        void HandleConnect();

        /**
         * @brief Processes pending connection closures from the close list
         * @note Safely removes connections marked for closure and releases resources
         */
        void HandleCloseList();

        /**
         * @brief Dispatches epoll events to appropriate handlers
         * @param events Array of epoll events to process
         * @param nfds Number of ready file descriptors
         * @param utilTask Utility task manager for operation dispatching
         */
        void HandleEvents(epoll_event *events, int nfds, UtilTask *utilTask);

        /**
         * @brief Processes read events for a connection
         * @param controller Connection controller triggering the read event
         * @param utilTask Utility task manager for dispatching read operations
         * @note May trigger user-defined read callback or initiate connection closure
         */
        void HandleRead(SOCKController *controller, UtilTask *utilTask);

        /**
         * @brief Processes write events for a connection
         * @param controller Connection controller triggering the write event
         * @param utilTask Utility task manager for dispatching write operations
         * @note May trigger user-defined write callback or initiate connection closure
         */
        void HandleWrite(SOCKController *controller, UtilTask *utilTask);

        /**
         * @brief Closes a connection and cleans up resources
         * @param fd File descriptor of the connection to close
         * @return Connection information string (IP:PORT format)
         */
        std::string CloseConnection(int fd);

        /**
         * @brief Closes a connection using its controller pointer
         * @param controller Pointer to the connection controller to close
         * @return Connection information string (IP:PORT format)
         * @note Removes from epoll monitoring and cleans up associated resources
         */
        std::string CloseConnection(SOCKController *controller);

        /**
         * @brief Performs comprehensive cleanup of all network resources
         * @note Closes all active connections, listening sockets, and epoll instances.
         *       Called during graceful shutdown or instance destruction.
         */
        void Clean();

        /**
         * @brief Private constructor
         */
        SPSockTcp();

    public:
        /**
         * @brief Initialize global configuration parameters
         * @param config Reference to the configuration parameters
         * @note Must be called before creating any instance to ensure proper initialization
         */
        static void Config(SPConfig config = {16 * 1024, 32 * 1024, 16, 64, 5000, -1, EPOLLIN, 10000, 4, 10, 5, LOG_LEVEL_WARNING});

        /**
         * @brief Gets singleton instance
         * @note Not thread-safe
         * @return Pointer to the singleton instance
         */
        static SPSockTcp *GetInstance();

        /**
         * @brief Starts listening on specified port
         * @param port Port number to listen on
         * @return true on success, false on failure
         * @note One-time call function
         */
        bool Listen(unsigned short port) SPSOCK_ONE_TIME_CALL;

        /**
         * @brief Main event processing loop
         * @return true on success, false on failure
         * @note One-time call function
         */
        bool EventLoop() SPSOCK_ONE_TIME_CALL;

        /**
         * @brief Configures linger options
         * @param enable Enable/disable lingering
         * @param waitSeconds Linger timeout (ignored if disabled)
         * @return true on success, false on failure
         */
        bool EnableLinger(bool enable, int waitSeconds = 5);

        /**
         * @brief Configures TCP keep-alive options
         * @param enable Enable/disable keep-alive
         * @param aliveSeconds Idle time before probes
         * @param detectTimes Number of probes
         * @param detectInterval Interval between probes
         * @return true on success, false on failure
         */
        bool EnableKeepAlive(bool enable, int aliveSeconds = 120, int detectTimes = 3, int detectInterval = 10);

        /**
         * @brief Sets user-defined callbacks
         * @param cnp New connection callback
         * @param csp Close event callback
         * @param rdp Read event callback
         * @param wtp Write event callback
         * @return true on success, false on failure
         */
        bool SetCallback(ConnectProc cnp = nullptr, CloseProc csp = nullptr, ReadProc rdp = nullptr, WriteProc wtp = nullptr);

        /**
         * @brief Configures watermarks and timeout thresholds for read/write event triggering.
         * @param readMark Minimum number of bytes in the receive buffer to trigger a read event (0 = immediate).
         * @param writeMark Minimum free space in the send buffer to trigger a write event (0 = immediate).
         */
        void SetWaterMark(unsigned int readMark = 0, unsigned int writeMark = 0);

        /**
         * @brief Configures exit signal handling
         * @param sg Signal number to handle
         * @param etp Event loop exit callback
         * @param ctx Context for exit callback
         * @return true on success, false on failure
         * @note All connections are closed when exiting via a signal.
         * @note Therefore, you are allowed to call ExitProc before that to clean up the reference to the connection resource
         */
        bool SetSignalExit(int sg, ExitProc etp = nullptr, void *ctx = nullptr);

        /**
         * @brief Signals event loop to exit
         * @note Should be called after starting event loop
         */
        static void SetExitFlag();

        /**
         * @brief Releases singleton resources
         */
        static void Release();
    };

    /**
     * @brief Main UDP socket management class
     * @tparam address_family IP version (IPv4/IPv6)
     */
    template <ADDRESS_FAMILY address_family>
    class SPSockUdp : noncopyable
    {
    private:
        void *ctx;    ///< User-defined context for callbacks
        RecvProc rcp; ///< Receive event callback

        int sockfd;          ///< Socket file descriptor
        unsigned int status; ///< Internal status flags

        static void *exitCtx;                       ///< Event loop exit context
        static ExitProc exitProc;                   ///< Event loop exit procedure
        static std::atomic<bool> exitFlag;          ///< Event loop control flag
        static SPSockUdp<address_family> *instance; ///< Singleton instance

        /**
         * @brief Cleans up socket resources
         */
        void Clean();

        /**
         * @brief Signal handler for graceful shutdown
         * @param sg Signal number received
         */
        static void HandleExit(int sg);

        /**
         * @brief Private constructor
         */
        SPSockUdp();

    public:
        /**
         * @brief Configures minimum logging level
         * @param minlevel Minimum log level to output
         */
        static void Config(LOG_LEVEL minlevel = LOG_LEVEL_WARNING);

        /**
         * @brief Gets singleton instance
         * @note Not thread-safe
         * @return Pointer to the singleton instance
         */
        static SPSockUdp *GetInstance();

        /**
         * @brief Binds socket to a specified port
         * @param port Port number to bind to
         * @return true on success, false on failure
         * @note One-time call function
         */
        bool Bind(unsigned short port) SPSOCK_ONE_TIME_CALL;

        /**
         * @brief Main event processing loop for UDP
         * @return true on success, false on failure
         * @note One-time call function
         */
        bool EventLoop() SPSOCK_ONE_TIME_CALL;

        /**
         * @brief Sends data to a specified IP and port
         * @param data Data buffer to send
         * @param size Size of data to send
         * @param ip Destination IP address
         * @param port Destination port number
         * @return true on success, false on failure
         */
        bool SendTo(const void *data, size_t size, const char *ip, unsigned short port);

        /**
         * @brief Configures exit signal handling
         * @param sg Signal number to handle
         * @param etp Event loop exit callback
         * @param ctx Context for exit callback
         * @return true on success, false on failure
         * @note All connections are closed when exiting via a signal.
         * @note Therefore, you are allowed to call ExitProc before that to clean up the reference to the connection resource
         */
        bool SetSignalExit(int sg, ExitProc etp = nullptr, void *ctx = nullptr);

        /**
         * @brief Sets receive callback
         * @param rcp Receive event callback
         * @param ctx User-defined context (optional)
         * @return true on success, false on failure
         */
        bool SetCallback(RecvProc rcp, void *ctx = nullptr);

        /**
         * @brief Signals event loop to exit
         * @note Should be called after starting event loop
         */
        static void SetExitFlag();

        /**
         * @brief Releases singleton resources
         */
        static void Release();
    };
}

#endif