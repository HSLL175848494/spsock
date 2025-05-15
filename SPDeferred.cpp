#include "SPDeferred.h"

namespace HSLL::DEFER
{
    void SPDefered::REnableFunc(SOCKController *controller)
    {
        if (!controller->renableEvents())
            controller->close();
    }

    void *recvCtx;
    RecvProc funcRecv;
    FuncFree funcFree;
    FuncClose funcClose;
    FuncEvent funcEvent;
    SPTcpConfig tcpConfig;
    SPUdpConfig udpConfig;
    SPWaterMark markGlobal{0, 0};
    REnableProc renableProc = SPDefered::REnableFunc;
}

namespace HSLL
{
    SPTcpBufferPool SPTcpBufferPool::pool;
    SPUdpBufferPool SPUdpBufferPool::pool;
}