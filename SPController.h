#ifndef HSLL_SPCONTROLLER
#define HSLL_SPCONTROLLER

#include <arpa/inet.h>
#include <string>

#include "SPTypes.h"
#include "SPBuffer.hpp"

namespace HSLL
{
    /**
     * @brief Controller class for socket operations
     * @note Provides thread-safe I/O operations and connection management
     */
    class SOCKController
    {
    public:

        /**
         * @brief Gets the context pointer associated with this controller
         * @return The context pointer
         */
        void *getCtx();

        /**
         * @brief Checks whether the peer (remote endpoint) has closed the connection
         * @return true if the peer has closed the connection, false otherwise
         */
        bool isPeerClosed();

        /**
         * @brief Reads data from the read buffer
         * @param buf Buffer to store the read data
         * @param len Maximum number of bytes to read
         * @return Number of bytes actually read
         */
        size_t read(void *buf, size_t len);

        /**
         * @brief Writes data directly to the socket
         * @param buf Data buffer to send
         * @param len Number of bytes to send
         * @return Number of bytes sent, 0 for EAGAIN/EWOULDBLOCK, -1 for errors
         * @note Call Close() or EnableEvent() on error return
         */
        ssize_t write(const void *buf, size_t len);

        /**
         * @brief Writes data to the write buffer (temporary storage)
         * @param buf Data buffer to write
         * @param len Number of bytes to write
         * @return Number of bytes actually written to the buffer
         */
        size_t writeTemp(const void *buf, size_t len);

        /**
         * @brief Commits buffered writes to the socket
         * @return Number of bytes remaining in buffer (0 if all sent),
         *         -1 on error
         * @note On error, call Close() or EnableEvent()
         */
        ssize_t commitWrite();

        /**
         * @brief Get the size of data available in the read buffer
         * @return Number of bytes available to read
         */
        unsigned int getReadBufferSize();

        /**
         * @brief Get the size of data pending in the write buffer
         * @return Number of bytes waiting to be sent
         */
        unsigned int getWriteBufferSize();

        /**
         * @brief Directly write back data from read buffer to socket
         *
         * This function first sends any pending data in the write buffer. If the write buffer
         * is completely emptied, it then attempts to send data directly from the read buffer
         * to the socket. Unsent data remains in the read buffer and is not moved to write buffer.
         *
         * @return true if operation succeeded (including partial writes),
         * @return false if socket error occurred (connection should be closed)
         */
        bool writeBack();

        /**
         * @brief Move data from read buffer to write buffer
         *
         * Transfers as much data as possible from the read buffer to the write buffer
         * without involving actual I/O operations. This is useful for implementing
         * echo services or data reflection patterns.
         *
         * @return size_t Number of bytes actually moved between buffers
         */
        size_t moveToWriteBuffer();

        /**
         * @brief Re-enables event monitoring for the socket
         * @param read Enable read events
         * @param write Enable write events
         * @return true on success, false on failure (requires Close())
         */
        bool enableEvents(bool read = false, bool write = false);

        /**
         * @brief Re-enables event monitoring with previously configured events
         * @return true on success, false on failure (requires Close())
         */
        bool renableEvents();

        /**
         * @brief Closes the connection actively
         * @note Should be called after detecting errors
         */
        void close();

    private:
        template <ADDRESS_FAMILY>
        friend class SPSockTcp;
        friend class SPInitializer;

        int fd;                               ///< Socket file descriptor
        int events;                           ///< Bitmask of currently active epoll events (EPOLLIN/EPOLLOUT)
        bool peerClosed;                      ///< Whether the peer (remote endpoint) has closed the connection
        void *ctx;                            ///< Context pointer for callback functions
        unsigned short port;                  ///< Port number for the socket connection
        char ip[INET6_ADDRSTRLEN];            ///< IP address string (IPv4 or IPv6)
        std::string ipPort;                   ///< Combined IP:port string for identification
        FuncClose fc;                         ///< Close callback function
        FuncEvent fe;                         ///< Event control function
        SPBuffer readBuf{BUFFER_TYPE_READ};   ///< Buffer for incoming data
        SPBuffer writeBuf{BUFFER_TYPE_WRITE}; ///< Buffer for outgoing data

        /**
         * @brief Initializes the controller with socket parameters
         * @param fd Socket file descriptor
         * @param ctx Context pointer for callbacks
         * @param fc Close callback function
         * @param fe Event control function
         * @param events Initial epoll event subscriptions
         */
        bool init(int fd, void *ctx, FuncClose fc, FuncEvent fe, int events);

        /**
         * @brief Reads data from the socket
         * @param buf Buffer to store received data
         * @param len Maximum bytes to read
         * @return Number of bytes read, 0 for EAGAIN/EWOULDBLOCK, -1 for errors
         * @note Call Close() or EnableEvent() on error return
         */
        ssize_t readInner(void *buf, size_t len);

        /**
         * @brief Writes data to the socket
         * @param buf Data buffer to send
         * @param len Number of bytes to send
         * @return Number of bytes sent, 0 for EAGAIN/EWOULDBLOCK, -1 for errors
         * @note Call Close() or EnableEvent() on error return
         */
        ssize_t writeInner(const void *buf, size_t len);

        /**
         * @brief Reads data from socket into the read buffer
         * @return true if read was successful or would block, false on error
         * @note On error, call Close() or EnableEvent()
         */
        bool readSocket();
    };
}

#endif // HSLL_SPCONTROLLER