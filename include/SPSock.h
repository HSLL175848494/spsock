
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
         * @brief Configures watermark thresholds for triggering read/write events.
         * Read events are triggered when the read buffer contains at least readMark bytes of data.
         * Write events are triggered when the amount of pending data in the write buffer falls below
         * or equals to writeMark, indicating available space for new data to send.
         * @param readMark Minimum number of bytes required in the read buffer to trigger a read event (0 = trigger immediately).
         * @param writeMark Maximum allowed pending data in the write buffer to trigger a write event (0xffffffff = trigger immediately).
         */
        void SetWaterMark(unsigned int readMark = 0, unsigned int writeMark = 0xffffffff);

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