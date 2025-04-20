#ifndef HSLL_SPCPNTROLLER
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
        typedef void (*FuncClose)(void *, int);             ///< Close callback function type
        typedef bool (*FuncEvent)(void *, int, bool, bool); ///< Event control function type

        int fd;       ///< Socket file descriptor
        void *ctx;    ///< Context pointer for callback functions
        FuncClose fc; ///< Close callback function
        FuncEvent fe; ///< Event control function

        template <ADDRESS_FAMILY>
        friend class SPSockTcp;

        /**
         * @brief Private constructor used by SPSockTcp
         * @param fd Socket file descriptor
         * @param ctx Context pointer for callbacks
         * @param fc Close callback function
         * @param fe Event control callback function
         */
        SOCKController(int fd, void *ctx, FuncClose fc, FuncEvent fe)
            : fd(fd), ctx(ctx), fc(fc), fe(fe) {}

    public:
        /**
         * @brief Default constructor
         */
        SOCKController() {}

        /**
         * @brief Reads data from the socket
         * @param buf Buffer to store received data
         * @param size Maximum bytes to read
         * @return Number of bytes read, 0 for EAGAIN/EWOULDBLOCK, -1 for errors
         * @note Call Close() or EnableEvent() on error return
         */
        ssize_t Read(void *buf, size_t size)
        {
        retry:
            ssize_t ret = recv(fd, buf, size, 0);
            if (ret == -1)
            {
                if (errno == EINTR)
                    goto retry;
                else if (errno == EAGAIN || errno == EWOULDBLOCK)
                    return 0;
                else
                    return -1;
            }
            return ret;
        }

        /**
         * @brief Writes data to the socket
         * @param buf Data buffer to send
         * @param size Number of bytes to send
         * @return Number of bytes sent, 0 for EAGAIN/EWOULDBLOCK, -1 for errors
         * @note Call Close() or EnableEvent() on error return
         */
        ssize_t Write(const void *buf, size_t size)
        {
        retry:
            ssize_t ret = send(fd, buf, size, MSG_NOSIGNAL);
            if (ret == -1)
            {
                if (errno == EINTR)
                    goto retry;
                else if (errno == EAGAIN || errno == EWOULDBLOCK)
                    return 0;
                else
                    return -1;
            }
            return ret;
        }

        /**
         * @brief Re-enables event monitoring for the socket
         * @param read Enable read events
         * @param write Enable write events
         * @return true on success, false on failure (requires Close())
         */
        bool EnableEvent(bool read, bool write)
        {
            return fe(ctx, fd, read, write);
        }

        /**
         * @brief Closes the connection actively
         * @note Should be called after detecting errors
         */
        void Close()
        {
            fc(ctx, fd);
        }
    };
}

#endif