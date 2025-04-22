#ifndef HSLL_SPCPNTROLLER
#define HSLL_SPCONTROLLER

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
        typedef void (*FuncClose)(int);             ///< Close callback function type
        typedef bool (*FuncEvent)(int, bool, bool); ///< Event control function type

        int fd;                    ///< Socket file descriptor
        int events;                ///< Bitmask of currently active epoll events (EPOLLIN/EPOLLOUT)
        void *ctx;                 ///< Context pointer for callback functions
        unsigned short port;       ///< Port number for the socket connection
        char ip[INET6_ADDRSTRLEN]; ///< IP address string (IPv4 or IPv6)
        std::string ipPort;        ///< Combined IP:port string for identification

        FuncClose fc;      ///< Close callback function
        FuncEvent fe;      ///< Event control function
        SPBuffer readBuf;  ///< Buffer for incoming data
        SPBuffer writeBuf; ///< Buffer for outgoing data

        template <ADDRESS_FAMILY>
        friend class SPSockTcp;
        friend class SPInitializer;

        /**
         * @brief Initializes the controller with socket parameters
         * @param fd Socket file descriptor
         * @param ctx Context pointer for callbacks
         * @param fc Close callback function
         * @param fe Event control function
         * @param rbSize Read buffer size
         * @param wbSize Write buffer size
         * @param events Initial epoll event subscriptions
         */
        void init(int fd, void *ctx, FuncClose fc, FuncEvent fe, unsigned int rbSize, unsigned int wbSize, int events)
        {
            this->fd = fd;
            this->ctx = ctx;
            this->fc = fc;
            this->fe = fe;
            this->readBuf.Init(rbSize);
            this->writeBuf.Init(wbSize);
            this->events = events;
            ipPort = "[" + std::string(ip) + "]:" + std::to_string(port);
        }

        /**
         * @brief Reads data from the socket
         * @param buf Buffer to store received data
         * @param len Maximum bytes to read
         * @return Number of bytes read, 0 for EAGAIN/EWOULDBLOCK, -1 for errors
         * @note Call Close() or EnableEvent() on error return
         */
        ssize_t readInner(void *buf, size_t len)
        {
        retry:
            ssize_t ret = recv(fd, buf, len, 0);
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
         * @param len Number of bytes to send
         * @return Number of bytes sent, 0 for EAGAIN/EWOULDBLOCK, -1 for errors
         * @note Call Close() or EnableEvent() on error return
         */
        ssize_t writeInner(const void *buf, size_t len)
        {
        retry:
            ssize_t ret = send(fd, buf, len, MSG_NOSIGNAL);
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
         * @brief Reads data from socket into the read buffer
         * @return true if read was successful or would block, false on error
         * @note On error, call Close() or EnableEvent()
         */
        bool readSocket()
        {
            unsigned int len;

            while (len = readBuf.distanceWrite())
            {
                ssize_t bytes = readInner(readBuf.writePtr(), len);

                if (bytes > 0)
                    readBuf.commitWrite(bytes);
                else if (bytes == 0)
                    return true;
                else
                    return false;

                if (bytes < len)
                    return true;
            }
            return true;
        }

    public:
        /**
         * @brief Gets the context pointer associated with this controller
         * @return The context pointer
         */
        void *getCtx()
        {
            return ctx;
        }

        /**
         * @brief Reads data from the read buffer
         * @param buf Buffer to store the read data
         * @param len Maximum number of bytes to read
         * @return Number of bytes actually read
         */
        size_t read(void *buf, size_t len)
        {
            return readBuf.read(buf, len);
        }

        /**
         * @brief Writes data directly to the socket
         * @param buf Data buffer to send
         * @param len Number of bytes to send
         * @return Number of bytes sent, 0 for EAGAIN/EWOULDBLOCK, -1 for errors
         * @note Call Close() or EnableEvent() on error return
         */
        ssize_t write(const void *buf, size_t len)
        {
        retry:
            ssize_t ret = send(fd, buf, len, MSG_NOSIGNAL);
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
         * @brief Writes data to the write buffer (temporary storage)
         * @param buf Data buffer to write
         * @param len Number of bytes to write
         * @return Number of bytes actually written to the buffer
         */
        size_t writeTemp(const void *buf, size_t len)
        {
            return writeBuf.write(buf, len);
        }

        /**
         * @brief Commits buffered writes to the socket
         * @return Number of bytes remaining in buffer (0 if all sent),
         *         -1 on error
         * @note On error, call Close() or EnableEvent()
         */
        ssize_t commitWrite()
        {
            unsigned int len;

            while (len = writeBuf.distanceRead())
            {
                ssize_t bytes = writeInner(writeBuf.readPtr(), len);

                if (bytes > 0)
                    writeBuf.commitRead(bytes);
                else if (bytes == 0)
                    return writeBuf.bytesRead();
                else
                    return -1;

                if (bytes < len)
                    return writeBuf.bytesRead();
            }
            return 0;
        }

        /**
         * @brief Re-enables event monitoring for the socket
         * @param read Enable read events
         * @param write Enable write events
         * @return true on success, false on failure (requires Close())
         */
        bool enableEvents(bool read = false, bool write = false)
        {
            bool ret = fe(fd, read, write);
            events = 0;
            if (ret)
            {
                if (read)
                    events |= EPOLLIN;
                if (write)
                    events |= EPOLLOUT;
            }
            return ret;
        }

        /**
         * @brief Re-enables event monitoring with previously configured events
         * @return true on success, false on failure (requires Close())
         */
        bool renableEvents()
        {
            bool ret = fe(fd, events & EPOLLIN, events & EPOLLOUT);
            if (!ret)
                events = 0;
            return ret;
        }

        /**
         * @brief Closes the connection actively
         * @note Should be called after detecting errors
         */
        void close()
        {
            fc(fd);
        }
    };
}

#endif