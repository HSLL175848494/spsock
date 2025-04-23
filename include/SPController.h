#ifndef HSLL_SPCONTROLLER
#define HSLL_SPCONTROLLER

#include "SPTypes.h"

namespace HSLL
{
    /**
     * @brief Controller class for socket operations
     * @note Provides thread-safe I/O operations and connection management
     */
    class SOCKController
    {
        typedef void (*FuncClose)(int);             ///< Close callback function type
        typedef bool (*FuncEvent)(int, bool, bool); ///< Event control function type

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
    };
}

#endif