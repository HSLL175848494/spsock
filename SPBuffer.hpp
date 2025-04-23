#ifndef HSLL_BUFFER
#define HSLL_BUFFER

#include <string.h>

namespace HSLL
{

    /**
     * @brief Circular buffer implementation for efficient I/O operations
     *
     * This class provides a thread-unsafe circular buffer with separate
     * read and write pointers, optimized for network I/O operations.
     */
    class SPBuffer
    {
        unsigned int back = 0;           ///< Read position pointer (where data is consumed from)
        unsigned int front = 0;          ///< Write position pointer (where data is added)
        unsigned int size = 0;           ///< Current number of bytes stored in buffer
        unsigned int bsize = 0;          ///< Total capacity of the buffer
        unsigned char *buffer = nullptr; ///< Underlying data storage

    public:
        /**
         * @brief Initialize the buffer with specified capacity
         * @param size The total capacity of the buffer in bytes
         */
        void Init(unsigned int size)
        {
            bsize = size;
            buffer = new unsigned char[size];
        }

        /**
         * @brief Get the number of bytes available to read
         * @return Number of bytes currently stored in buffer
         */
        unsigned int bytesRead()
        {
            return size;
        }

        /**
         * @brief Get the number of bytes available for writing
         * @return Remaining free space in buffer in bytes
         */
        unsigned int bytesWrite()
        {
            return bsize - size;
        }

        /**
         * @brief Get the linear space available for writing
         * @return Number of contiguous bytes available from write pointer
         *         to either end of buffer or wrap-around point
         */
        unsigned int distanceWrite()
        {
            const unsigned int writeAvailable = bytesWrite();
            const unsigned int linearSpace = bsize - front;
            return (linearSpace > writeAvailable) ? writeAvailable : linearSpace;
        }

        /**
         * @brief Get the linear space available for reading
         * @return Number of contiguous bytes available from read pointer
         *         to either end of buffer or wrap-around point
         */
        unsigned int distanceRead()
        {
            const unsigned int readAvailable = bytesRead();
            const unsigned int linearSpace = bsize - back;
            return (linearSpace > readAvailable) ? readAvailable : linearSpace;
        }

        /**
         * @brief Commit read operations (advance read pointer)
         * @param len Number of bytes to advance read pointer
         */
        void commitRead(unsigned int len)
        {
            back = (back + len) % bsize;
            size -= len;
        }

        /**
         * @brief Commit write operations (advance write pointer)
         * @param len Number of bytes to advance write pointer
         */
        void commitWrite(unsigned int len)
        {
            front = (front + len) % bsize;
            size += len;
            if (size == 0)
                front = back = 0;
        }

        /**
         * @brief Get direct write pointer
         * @return Pointer to current write position
         * @warning Must check bytesWrite() before using
         */
        unsigned char *writePtr()
        {
            return buffer + front;
        }

        /**
         * @brief Get direct read pointer
         * @return Pointer to current read position
         * @warning Must check bytesRead() before using
         */
        unsigned char *readPtr()
        {
            return buffer + back;
        }

        /**
         * @brief Read data from buffer
         * @param buf Destination buffer for read data
         * @param len Maximum number of bytes to read
         * @return Actual number of bytes read
         */
        unsigned int read(void *buf, unsigned int len)
        {
            if (len == 0 || size == 0)
                return 0;

            const unsigned int bytesToRead = (len > size) ? size : len;
            const unsigned int firstChunk = (back + bytesToRead > bsize) ? (bsize - back) : bytesToRead;
            memcpy(buf, buffer + back, firstChunk);

            if (firstChunk < bytesToRead)
                memcpy((unsigned char *)buf + firstChunk, buffer, bytesToRead - firstChunk);

            size -= bytesToRead;
            if (size == 0)
            {
                front = back = 0;
            }
            else
            {
                back = (back + bytesToRead) % bsize;
            }

            return bytesToRead;
        }

        /**
         * @brief Write data to buffer
         * @param buf Source buffer containing data to write
         * @param len Number of bytes to write
         * @return Actual number of bytes written
         */
        unsigned int write(const void *buf, unsigned int len)
        {
            if (len == 0 || bytesWrite() == 0)
                return 0;

            const unsigned int bytesToWrite = (len > bytesWrite()) ? bytesWrite() : len;
            const unsigned int firstChunk = (front + bytesToWrite > bsize) ? (bsize - front) : bytesToWrite;
            memcpy(buffer + front, buf, firstChunk);

            if (firstChunk < bytesToWrite)
                memcpy(buffer, (const unsigned char *)buf + firstChunk, bytesToWrite - firstChunk);

            front = (front + bytesToWrite) % bsize;
            size += bytesToWrite;
            return bytesToWrite;
        }

        /**
         * @brief Destructor - releases buffer memory
         */
        ~SPBuffer()
        {
            if (buffer)
                delete[] buffer;
        }
    };
}

#endif