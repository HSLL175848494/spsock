#ifndef HSLL_BUFFER
#define HSLL_BUFFER

namespace HSLL
{
    /**
     * @brief Circular buffer implementation for efficient I/O operations
     * This class provides a thread-unsafe circular buffer with separate
     * read and write pointers, optimized for network I/O operations
     */
    class SPBuffer
    {
    public:
        /**
         * @brief Get the number of bytes available to read
         * @return Number of bytes currently stored in buffer
         */
        unsigned int bytesRead();

        /**
         * @brief Get the number of bytes available for writing
         * @return Remaining free space in buffer in bytes
         */
        unsigned int bytesWrite();

        /**
         * @brief Get the linear space available for writing
         * @return Number of contiguous bytes available from write pointer
         *         to either end of buffer or wrap-around point
         */
        unsigned int distanceWrite();

        /**
         * @brief Get the linear space available for reading
         * @return Number of contiguous bytes available from read pointer
         *         to either end of buffer or wrap-around point
         */
        unsigned int distanceRead();

        /**
         * @brief Commit read operations (advance read pointer)
         * @param len Number of bytes to advance read pointer
         */
        void commitRead(unsigned int len);

        /**
         * @brief Commit write operations (advance write pointer)
         * @param len Number of bytes to advance write pointer
         */
        void commitWrite(unsigned int len);

        /**
         * @brief Get direct write pointer
         * @return Pointer to current write position
         * @warning Must check bytesWrite() before using
         */
        unsigned char *writePtr();

        /**
         * @brief Get direct read pointer
         * @return Pointer to current read position
         * @warning Must check bytesRead() before using
         */
        unsigned char *readPtr();

        /**
         * @brief Read data from buffer
         * @param buf Destination buffer for read data
         * @param len Maximum number of bytes to read
         * @return Actual number of bytes read
         */
        unsigned int read(void *buf, unsigned int len);

        /**
         * @brief Peek data from buffer without advancing read pointer
         * @param buf Destination buffer for peeked data
         * @param len Maximum number of bytes to peek
         * @return Actual number of bytes peeked
         * @warning Same constraints as read() - check bytesRead() first
         */
        unsigned int peek(void *buf, unsigned int len);

        /**
         * @brief Write data to buffer
         * @param buf Source buffer containing data to write
         * @param len Number of bytes to write
         * @return Actual number of bytes written
         */
        unsigned int write(const void *buf, unsigned int len);
    };
}

#endif