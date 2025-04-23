
#include "SPController.h"

namespace HSLL
{
    // SOCKController Implementation
    void SOCKController::init(int fd, void *ctx, FuncClose fc, FuncEvent fe,
                              unsigned int rbSize, unsigned int wbSize, int events)
    {
        this->fd = fd;
        this->ctx = ctx;
        this->fc = fc;
        this->fe = fe;
        this->readBuf.Init(rbSize);
        this->writeBuf.Init(wbSize);
        this->events = events;
        peerClosed = false;
        ipPort = "[" + std::string(ip) + "]:" + std::to_string(port);
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
                return -1;

            if (bytes < len)
                return writeBuf.bytesRead();
        }
        return 0;
    }

    unsigned int SOCKController::getReadBufferSize()
    {
        return readBuf.bytesRead();
    }

    unsigned int SOCKController::getWriteBufferSize()
    {
        return writeBuf.bytesRead();
    }

    bool SOCKController::writeBack()
    {
        ssize_t writeResult = commitWrite();

        if (writeResult == -1)
            return false;

        if (writeResult > 0)
            return true;

        while (true)
        {
            unsigned int chunk = readBuf.distanceRead();
            if (chunk == 0)
                break;

            ssize_t sent = writeInner(readBuf.readPtr(), chunk);
            if (sent > 0)
            {
                readBuf.commitRead(sent);
            }
            else if (sent == 0)
            {
                break;
            }
            else
            {
                return false;
            }
        }
        return true;
    }

    size_t SOCKController::moveToWriteBuffer()
    {
        size_t moved = 0;
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

    bool SOCKController::renableEvents()
    {
        bool ret = fe(fd, events & EPOLLIN, events & EPOLLOUT);
        if (!ret)
            events = 0;
        return ret;
    }

    void SOCKController::close()
    {
        fc(fd);
    }
}