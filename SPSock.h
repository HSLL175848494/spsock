#ifndef HSLL_SPSOCK
#define HSLL_SPSOCK

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <unordered_map>
#include <unordered_set>

#include "SPLog.hpp"
#include "SPInitializer.h"

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
     * @brief Socket base class
     */
    class SPSock : noncopyable
    {
    public:
        /**
         * @brief Initialize global configuration parameters
         * @param config Reference to the configuration parameters
         * @note Must be called before creating any instance to ensure proper initialization
         */
        static void Config(SPConfig config = {16 * 1024, 32 * 1024, 16, 64, 5000, -1, EPOLLIN, 10000, 4, 10, 5, LOG_LEVEL_WARNING});
    };

    /**
     * @brief Main TCP socket management class
     * @tparam address_family IP version (IPv4/IPv6)
     */
    template <ADDRESS_FAMILY address_family>
    class SPSockTcp : public SPSock
    {
    private:
        linger lin;        ///< Linger configuration
        SPSockProc proc;   ///< User connections callbacks
        SPSockAlive alive; ///< Keep-alive settings

        CloseList list;                                      ///< Connection close list
        std::unordered_map<int, SOCKController> connections; ///< Active connections

        int status;   ///< Internal status flags
        int idlefd;   ///< Idle file descriptor
        int epollfd;  ///< Epoll file descriptor
        int listenfd; ///< Listening socket descriptor

        static void *exitCtx;                       ///< Event loop exit ctx
        static ExitProc exitProc;                   ///< Event loop exit proc
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
         * @brief Handles new incoming connections
         */
        void DealConnect();

        /**
         * @brief Signal handler for graceful shutdown
         */
        static void DealExit(int sg);

        /**
         * @brief Close callback implementation
         * @param fd File descriptor to close
         */
        static void FuncClose(int fd);

        /**
         * @brief Event control callback implementation
         * @param fd File descriptor to modify
         * @param read Enable read events
         * @param write Enable write events
         * @return true on success, false on failure
         */
        static bool FuncEnableEvent(int fd, bool read, bool write);

        /**
         * @brief Closes a connection and cleans up resources
         * @param fd File descriptor of the connection to close
         * @return Connection information string
         */
        std::string CloseConnection(int fd);

        /**
         * @brief Cleans up all connections and resources
         */
        void Clean();

        /**
         * @brief Private constructor
         */
        SPSockTcp();

    public:
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
         * @param policy Full load strategy, wait or abandon
         * @return true on success, false on failure
         * @note One-time call function
         */
        bool EventLoop(FULL_LOAD_POLICY policy = FULL_LOAD_POLICY_DISCARD) SPSOCK_ONE_TIME_CALL;

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
         * @brief Configures exit signal handling
         * @param sg Signal number to handle
         * @param etp Event loop exit Callback
         * @param ctx Context of ExitProc
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
    class SPSockUdp : public SPSock
    {
    private:
        void *ctx;    ///< User-defined context for callbacks
        RecvProc rcp; ///< Receive event callback

        int sockfd;          ///< Socket file descriptor
        unsigned int status; ///< Internal status flags

        static void *exitCtx;                       ///< Event loop exit ctx
        static ExitProc exitProc;                   ///< Event loop exit proc
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
        static void DealExit(int sg);

        /**
         * @brief Private constructor
         */
        SPSockUdp();

    public:
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
         */
        bool Bind(unsigned short port) SPSOCK_ONE_TIME_CALL;

        /**
         * @brief Main event processing loop for UDP
         * @param policy Full load policy
         * @return true on success, false on failure
         * @note One-time call function
         */
        bool EventLoop(FULL_LOAD_POLICY policy = FULL_LOAD_POLICY_DISCARD) SPSOCK_ONE_TIME_CALL;

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
         * @param etp Event loop exit Callback
         * @param ctx Context of ExitProc
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

    template <ADDRESS_FAMILY address_family>
    std::atomic<bool> SPSockTcp<address_family>::exitFlag = true;

    template <ADDRESS_FAMILY address_family>
    std::atomic<bool> SPSockUdp<address_family>::exitFlag = true;

    template <ADDRESS_FAMILY address_family>
    void *SPSockTcp<address_family>::exitCtx = nullptr;

    template <ADDRESS_FAMILY address_family>
    void *SPSockUdp<address_family>::exitCtx = nullptr;

    template <ADDRESS_FAMILY address_family>
    ExitProc SPSockTcp<address_family>::exitProc = nullptr;

    template <ADDRESS_FAMILY address_family>
    ExitProc SPSockUdp<address_family>::exitProc = nullptr;

    template <ADDRESS_FAMILY address_family>
    SPSockTcp<address_family> *SPSockTcp<address_family>::instance = nullptr;

    template <ADDRESS_FAMILY address_family>
    SPSockUdp<address_family> *SPSockUdp<address_family>::instance = nullptr;
}

#endif