
#ifndef HSLL_SPSOCK
#define HSLL_SPSOCK

#include <signal.h>
#include <sys/epoll.h>

#include "noncopyable.h"
#include "SPController.h"

namespace HSLL
{
    /**
     * @brief Main TCP socket management class
     * @tparam address_family IP version (IPv4/IPv6)
     */
    template <ADDRESS_FAMILY address_family>
    class SPSockTcp : noncopyable
    {
    public:
        /**
         * @brief Configures global runtime parameters
         * @param config Configuration structure with tuning parameters
         * @note Must be called before instance creation
         */
        static void Config(SPConfig config = {16 * 1024, 32 * 1024, 16, 64, 5000, -1, EPOLLIN, 10000, 10, 5, 0.6, LOG_LEVEL_WARNING});

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
     * @brief Main UDP socket management class
     * @tparam address_family IP version (IPv4/IPv6)
     */
    template <ADDRESS_FAMILY address_family>
    class SPSockUdp : noncopyable
    {
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