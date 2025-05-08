#include "SPDeferred.h"

namespace HSLL::DEFER
{
    void SPDefered::REnableFunc(SOCKController *controller)
    {
        if (!controller->renableEvents())
            controller->close();
    }

    FuncClose funcClose;
    FuncEvent funcEvent;
    SPConfig configGlobal;
    REnableProc renableProc = SPDefered::REnableFunc;
    SPWaterMark markGlobal{0, 0};
}

namespace HSLL
{
    SPBufferPool SPBufferPool::pool;
}