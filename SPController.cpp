
#include "SPController.h"

namespace HSLL
{
    // SOCKController Implementation
    bool SOCKController::init(int fd, void *ctx, IOThreadInfo *info)
    {
        if (!readBuf.Init())
            return false;

        if (!writeBuf.Init())
            return false;

        this->fd = fd;
        this->info = info;
        this->ctx = ctx;
        this->events = configGlobal.EPOLL_DEFAULT_EVENT;
        peerClosed = false;
        ipPort = "[" + std::string(ip) + "]:" + std::to_string(port);
        return true;
    }

    ssize_t SOCKController::readInner(void *buf, size_t len)
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

    ssize_t SOCKController::writeInner(const void *buf, size_t len)
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

    bool SOCKController::readSocket()
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

    void *SOCKController::getCtx()
    {
        return ctx;
    }

    bool SOCKController::isPeerClosed()
    {
        return peerClosed;
    }

    size_t SOCKController::read(void *buf, size_t len)
    {
        return readBuf.read(buf, len);
    }

    size_t SOCKController::peek(void *buf, size_t len)
    {
        return readBuf.peek(buf, len);
    }

    ssize_t SOCKController::write(const void *buf, size_t len)
    {
    retry:
        ssize_t ret = send(fd, buf, len, MSG_NOSIGNAL);
        if (ret == -1)
        {
            if (errno == EINTR)
                goto retry;
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;
            else if (errno == EPIPE || errno == ECONNRESET)
            {
                peerClosed = true;
                return -2;
            }
            else
                return -1;
        }
        return ret;
    }

    size_t SOCKController::writeTemp(const void *buf, size_t len)
    {
        return writeBuf.write(buf, len);
    }

    ssize_t SOCKController::commitWrite()
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
                return bytes;

            if (bytes < len)
                return writeBuf.bytesRead();
        }
        return 0;
    }

    size_t SOCKController::getReadBufferSize()
    {
        return readBuf.bytesRead();
    }

    size_t SOCKController::getWriteBufferSize()
    {
        return writeBuf.bytesRead();
    }

    SPBuffer *SOCKController::getReadBuffer()
    {
        return &readBuf;
    }

    SPBuffer *SOCKController::getWriteBuffer()
    {
        return &writeBuf;
    }

    size_t SOCKController::getReadBufferCapacity()
    {
        return configGlobal.READ_BSIZE;
    }

    size_t SOCKController::getWriteBufferCapacity()
    {
        return configGlobal.WRITE_BSIZE;
    }

    ssize_t SOCKController::writeBack()
    {
        ssize_t writeResult = commitWrite();

        if (writeResult != 0)
            return writeResult;

        while (true)
        {
            unsigned int chunk = readBuf.distanceRead();

            if (chunk == 0)
                break;

            ssize_t sent = writeInner(readBuf.readPtr(), chunk);

            if (sent > 0)
                readBuf.commitRead(sent);
            else if (sent == 0)
                break;
            else
                return sent;

            writeResult += sent;
        }
        return writeResult;
    }

    size_t SOCKController::moveToWriteBuffer()
    {
        unsigned int moved = 0;
        unsigned int maxMove = std::min(readBuf.bytesRead(), writeBuf.bytesWrite());

        while (moved < maxMove)
        {
            unsigned int readChunk = readBuf.distanceRead();
            unsigned int writeChunk = writeBuf.distanceWrite();
            unsigned int chunk = std::min(readChunk, writeChunk);

            if (chunk == 0)
                break;

            memcpy(writeBuf.writePtr(), readBuf.readPtr(), chunk);
            readBuf.commitRead(chunk);
            writeBuf.commitWrite(chunk);
            moved += chunk;
        }
        return moved;
    }

    bool SOCKController::enableEvents(bool read, bool write)
    {
        bool ret = DEFER::funcEvent(this, read, write);
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

    bool SOCKController::renableEvents()
    {
        bool ret = DEFER::funcEvent(this, events & EPOLLIN, events & EPOLLOUT);
        if (!ret)
            events = 0;
        return ret;
    }

    void SOCKController::close()
    {
        DEFER::funcClose(this);
    }
}