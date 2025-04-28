#include "SPInitializer.h"

namespace HSLL
{
    void SPInitializer::TaskFunc(SOCKController *ctx, ReadWriteProc proc)
    {
        if (!ctx->readSocket())
        {
            if (!ctx->enableEvents(false, false))
                ctx->close();
            return;
        }

        if (ctx->isPeerClosed() && (!ctx->getReadBufferSize()))
        {
            ctx->close();
            return;
        }

        if (markGlobal.readMark == 0 || markGlobal.writeMark == 0)
        {
            proc(ctx);
            return;
        }

        const bool isRead = (ctx->event == EPOLLIN);
        long long bufferSize = isRead ? ctx->getReadBufferSize() : ctx->getWriteBufferSize();
        const long long mark = isRead ? markGlobal.readMark : markGlobal.writeMark;

        if (bufferSize > mark)
        {
            *(isRead ? &ctx->tvRead : &ctx->tvWrite) = ctx->getTimestamp();
            proc(ctx);
            return;
        }

        long long timeoutMills = isRead ? markGlobal.readTimeoutMills : markGlobal.writeTimeoutMills;
        long long lastEventTime = isRead ? ctx->tvRead : ctx->tvWrite;
        long long *timeStorage = isRead ? &ctx->tvRead : &ctx->tvWrite;

        if (timeoutMills == 0)
        {
            proc(ctx);
            return;
        }

        long long currentTime = ctx->getTimestamp();
        long long elapsed = currentTime - lastEventTime;

        if (elapsed >= timeoutMills)
        {
            *timeStorage = currentTime;
            proc(ctx);
        }
        else
        {
            if (!ctx->renableEvents())
                ctx->close();
        }
    }

    void SPInitializer::REnableFunc(SOCKController *controller)
    {
        if (!controller->renableEvents())
            controller->close();
    }

    SPConfig DEFER::configGlobal;
    SPWaterMark DEFER::markGlobal{0, 0, UINT32_MAX, UINT32_MAX};
    TaskProc DEFER::taskProc = SPInitializer::TaskFunc;
    REnableProc DEFER::renableProc = SPInitializer::REnableFunc;
    SPBufferPool SPBufferPool::pool;
}