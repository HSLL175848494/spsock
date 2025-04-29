#include "SPDeferred.h"

namespace HSLL::DEFER
{
    void SPDefered::TaskFunc(SOCKController *ctx, ReadWriteProc proc)
    {
        if (!ctx->readSocket())
        {
            if (!ctx->enableEvents(false, false))
                ctx->close();
            return;
        }

        if (ctx->isPeerClosed() && ctx->getReadBufferSize() == 0)
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
            proc(ctx);
            return;
        }
        else
        {
            if (!ctx->renableEvents())
                ctx->close();
        }
    }

    void SPDefered::REnableFunc(SOCKController *controller)
    {
        if (!controller->renableEvents())
            controller->close();
    }

    FuncClose funcClose;
    FuncEvent funcEvent;
    SPConfig configGlobal;
    TaskProc taskProc = SPDefered::TaskFunc;
    REnableProc renableProc = SPDefered::REnableFunc;
    SPWaterMark markGlobal{0, 0};
}

namespace HSLL
{
    SPBufferPool SPBufferPool::pool;
}