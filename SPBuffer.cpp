#include "SPBuffer.h"

namespace HSLL
{
    SPBuffer::SPBuffer(BUFFER_TYPE type)
        : back(0), front(0), size(0), bsize(0), buffer(nullptr), type(type) {}

    bool SPBuffer::Init()
    {
        if (type == BUFFER_TYPE_READ)
            bsize = configGlobal.READ_BSIZE;
        else
            bsize = configGlobal.WRITE_BSIZE;

        buffer = (unsigned char *)SPBufferPool::GetBuffer(type);
        return buffer != nullptr;
    }

    unsigned int SPBuffer::bytesRead()
    {
        return size;
    }

    unsigned int SPBuffer::bytesWrite()
    {
        return bsize - size;
    }

    unsigned int SPBuffer::distanceWrite()
    {
        const unsigned int writeAvailable = bytesWrite();
        const unsigned int linearSpace = bsize - front;
        return (linearSpace > writeAvailable) ? writeAvailable : linearSpace;
    }

    unsigned int SPBuffer::distanceRead()
    {
        const unsigned int readAvailable = bytesRead();
        const unsigned int linearSpace = bsize - back;
        return (linearSpace > readAvailable) ? readAvailable : linearSpace;
    }

    void SPBuffer::commitRead(unsigned int len)
    {
        back = (back + len) % bsize;
        size -= len;
    }

    void SPBuffer::commitWrite(unsigned int len)
    {
        front = (front + len) % bsize;
        size += len;
        if (size == 0)
            front = back = 0;
    }

    unsigned char *SPBuffer::writePtr()
    {
        return buffer + front;
    }

    unsigned char *SPBuffer::readPtr()
    {
        return buffer + back;
    }

    unsigned int SPBuffer::read(void *buf, unsigned int len)
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

    unsigned int SPBuffer::peek(void *buf, unsigned int len)
    {
        if (len == 0 || size == 0)
            return 0;

        const unsigned int bytesToRead = (len > size) ? size : len;
        const unsigned int firstChunk = (back + bytesToRead > bsize) ? (bsize - back) : bytesToRead;
        memcpy(buf, buffer + back, firstChunk);

        if (firstChunk < bytesToRead)
            memcpy((unsigned char *)buf + firstChunk, buffer, bytesToRead - firstChunk);

        return bytesToRead;
    }

    unsigned int SPBuffer::write(const void *buf, unsigned int len)
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

    SPBuffer::~SPBuffer()
    {
        if (buffer)
            SPBufferPool::FreeBuffer(buffer, type);
    }
}