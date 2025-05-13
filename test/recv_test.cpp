
#include "../SPSock.h"

using namespace HSLL;

void echo_read_write_proc(SOCKController *controller)
{
    if (controller->isPeerClosed())
    {
        controller->close();
        return;
    }

    unsigned char buf[1024 * 32];
    if (!controller->read(buf, 1024 * 32))
    {
        controller->close();
    }
    else
    {
        if (!controller->enableEvents(true, false))
            controller->close();
    }
}

int main() // g++ -o3  ../*.cpp recv_test.cpp -o test
{
    SPSockTcp<ADDRESS_FAMILY_INET>::Config({16 * 1024, 32 * 1024, 16, 64, 10000, -1, EPOLLIN, 20000, 10, 5, 0.9, LOG_LEVEL_INFO});

    auto ins = SPSockTcp<ADDRESS_FAMILY_INET>::GetInstance();

    if (ins->EnableKeepAlive(true, 120, 2, 10) == false)
        return -1;
    if (ins->SetCallback(nullptr, nullptr, echo_read_write_proc, echo_read_write_proc) == false)
        return -1;
    if (ins->SetSignalExit(SIGINT) == false)
        return -1;
    if (ins->Listen(4567) == false)
        return -1;

    ins->EventLoop();
    ins->Release();

    return 0;
}