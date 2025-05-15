
#include "../SPSock.h"

using namespace HSLL;

unsigned char buf[1024 * 32];

void echo_read_write_proc(SOCKController *controller)
{
    if (controller->isPeerClosed())
    {
        controller->close();
        return;
    }

    controller->writeTemp(buf, 1024 * 32);

    if (controller->commitWrite() == -1)
        controller->close();
    else if (!controller->enableEvents(false, true))
        controller->close();
}

int main() // g++ -o3  ../*.cpp write_test.cpp -o test
{
    //echo "Hello, UDP Server!" | nc -u 192.168.6.132 4567
    SPSockTcp<ADDRESS_FAMILY_INET>::Config({16 * 1024, 32 * 1024, 16, 64, 10000, EPOLLOUT, 20000, 10, 5, 0.9, LOG_LEVEL_INFO});

    auto ins = SPSockTcp<ADDRESS_FAMILY_INET>::GetInstance();

    if (ins->EnableKeepAlive(true, 120, 2, 10) == false)
        return -1;

    if (ins->SetCallback(nullptr, nullptr, echo_read_write_proc, echo_read_write_proc) == false)
        return -1;

    if (ins->SetSignalExit(SIGINT) == false)
        return -1;

    if (ins->Listen(4567) == false)
        return -1;

    ins->SetWaterMark(0, 4096);
    ins->EventLoop();
    ins->Release();

    return 0;
}