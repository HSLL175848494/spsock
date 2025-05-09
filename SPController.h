#ifndef HSLL_SPCONTROLLER
#define HSLL_SPCONTROLLER

#include <string>
#include <sys/time.h>
#include <arpa/inet.h>

#include "SPTypes.h"
#include "SPBuffer.h"

namespace HSLL
{

    /**
     * @brief Controller class for socket operations
     * @note Provides thread-safe I/O operations and connection management
     */
    class SOCKController
    {
        template <ADDRESS_FAMILY>
        friend class SPSockTcp;
        friend class DEFER::SPDefered;

        int fd;          ///< Socket file descriptor
        int events;      ///< Bitmask of currently active epoll events (EPOLLIN/EPOLLOUT)
        bool peerClosed; ///< Whether the peer (remote endpoint) has closed the connection

        void *ctx;                 ///< Context pointer for callback functions
        IOThreadInfo *info;        ///< Context pointer for i/o event loop
        char ip[INET6_ADDRSTRLEN]; ///< IP address string (IPv4 or IPv6)
        unsigned short port;       ///< Port number for the socket connection
        std::string ipPort;        ///< Combined IP:port string for identification

        SPBuffer readBuf{BUFFER_TYPE_READ};   ///< Buffer for incoming data
        SPBuffer writeBuf{BUFFER_TYPE_WRITE}; ///< Buffer for outgoing data

        /**
         * @brief Initializes the controller with socket parameters
         * @param fd Socket file descriptor
         * @param ctx Context pointer for callbacks
         * @param info Context pointer for i/o event loop
         */
        bool init(int fd, void *ctx, IOThreadInfo *info);

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

        /**
         * @brief Re-enables event monitoring with previously configured events
         * @return true on success, false on failure (requires Close())
         */
        bool renableEvents();

    public:
        /**
         * @brief Gets the context pointer associated with this controller
         * @return The context pointer
         */
        void *getCtx();

        /**
         * @brief Checks if connection is in half-closed state
         * @return true indicates:
         *         - Peer initiated shutdown sequence (FIN received)
         *         - Write operations are disabled
         *         - Read buffer may still contain available data
         *         - Connection should be closed after read buffer exhaustion
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
         * @brief Peeks data from the read buffer without advancing the read pointer
         * @param buf Destination buffer to copy data into
         * @param len Maximum number of bytes to peek
         * @return Number of bytes actually copied (0 if buffer is empty)
         * @note This is a non-destructive read operation
         */
        size_t peek(void *buf, size_t len);

        /**
         * @brief Writes data to the write buffer (temporary storage)
         * @param buf Data buffer to write
         * @param len Number of bytes to write
         * @return Number of bytes actually written to the buffer
         */
        size_t writeTemp(const void *buf, size_t len);

        /**
         * @brief Writes data directly to the socket
         * @param buf Data buffer to send
         * @param len Number of bytes to send
         * @return Number of bytes sent on success,
         *         0 for EAGAIN/EWOULDBLOCK (temporary congestion),
         *        -1 for unrecoverable errors (e.g., EBADF, ENOTSOCK),
         *        -2 if connection peer initiated shutdown (EPIPE/ECONNRESET)
         * @note When returning -2:
         *       - Read buffer may still contain pending data (check getReadBufferSize())
         *       - Subsequent write operations will fail
         *       - Must call Close() after consuming all read buffer data
         */
        ssize_t write(const void *buf, size_t len);

        /**
         * @brief Commits buffered writes to the socket
         * @return Number of bytes remaining in write buffer (0 if all sent),
         *        -1 for system errors,
         *        -2 if connection is half-closed by peer
         * @note For return code -2:
         *       - Connection is in half-closed state (FIN received)
         *       - Continue reading until getReadBufferSize() == 0
         *       - Must eventually call Close() to release resources
         */
        ssize_t commitWrite();

        /**
         * @brief Direct writeback from read buffer
         * @return Total bytes successfully written (>=0),
         *        -1 for system errors,
         *        -2 if connection reset by peer
         * @note When returning -2:
         *       - Read buffer preserves unprocessed data
         *       - Application should finalize read operations
         *       - Must call Close() after buffer processing
         */
        ssize_t writeBack();

        /**
         * @brief Get the size of data available in the read buffer
         * @return Number of bytes available to read
         */
        size_t getReadBufferSize();

        /**
         * @brief Get the size of data pending in the write buffer
         * @return Number of bytes waiting to be sent
         */
        size_t getWriteBufferSize();

        /**
         * @brief Get pointer to read buffer instance
         * @return Pointer to read buffer management object
         */
        SPBuffer *getReadBuffer();

        /**
         * @brief Get pointer to write buffer instance
         * @return Pointer to write buffer management object
         */
        SPBuffer *getWriteBuffer();

        /**
         * @brief Get read buffer capacity from config
         * @return Read buffer capacity in bytes defined by global config
         */
        size_t getReadBufferCapacity();

        /**
         * @brief Get write buffer capacity from config
         * @return Write buffer capacity in bytes defined by global config
         */
        size_t getWriteBufferCapacity();

        /**
         * @brief Move data from read buffer to write buffer
         *
         * Transfers as much data as possible from the read buffer to the write buffer
         * without involving actual I/O operations. This is useful for implementing
         * echo services or data reflection patterns.
         * @return size Number of bytes actually moved between buffers
         */
        size_t moveToWriteBuffer();

        /**
         * @brief Re-enables event monitoring for the socket
         * @param read Enable read events
         * @param write Enable write events
         * @return true on success, false on failure (requires Close())
         * @note This function and close() MUST NOT be called within the same callback
         *       unless this function returns false. If enableEvents fails, close()
         *       MUST be called immediately to clean up resources.
         */
        bool enableEvents(bool read = false, bool write = false);

        /**
         * @brief Close the connection actively
         * @note Should be called after detecting errors. This function and enableEvents()
         *       MUST NOT be both called within the same callback context unless enableEvents()
         *       has already returned false. Concurrent use may cause undefined behavior.
         */
        void close();
    };
}

#endif // HSLL_SPCONTROLLER