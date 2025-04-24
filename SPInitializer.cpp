#include "SPInitializer.h"

namespace HSLL
{
    void SPInitializer::TaskFunc(SOCKController *ctx, ReadWriteProc proc)
    {
        if (!ctx->readSocket())
            ctx->enableEvents(false, false);
        else
            proc(ctx);
    }

    void SPInitializer::REnableFunc(SOCKController *controller)
    {
        if (!controller->renableEvents())
            controller->close();
    }

    SPConfig DEFER::configGlobal;
    TaskProc DEFER::taskProc = SPInitializer::TaskFunc;
    REnableProc DEFER::renableProc = SPInitializer::REnableFunc;
    SPBufferPool SPBufferPool::pool;
}