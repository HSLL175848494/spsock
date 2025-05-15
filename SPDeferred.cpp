#include "SPDeferred.h"

namespace HSLL::DEFER
{
    void SPDefered::REnableFunc(SOCKController *controller)
    {
        if (!controller->renableEvents())
            controller->close();
    }

    LOG_LEVEL minLevel;
    FuncClose funcClose;
    FuncEvent funcEvent;
    SPTcpConfig tcpConfig;
    SPUdpConfig udpConfig;
    SPWaterMark markGlobal;
    REnableProc renableProc;
}

namespace HSLL
{
    SPTcpBufferPool SPTcpBufferPool::pool;
}