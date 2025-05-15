#include "../include/SPSock.h"
#include <atomic>

using namespace HSLL;

std::atomic<unsigned int> sum;

void echo_rcp(void *ctx, int fd, const char *data, size_t size, const char *ip, unsigned short port)
{
    auto ins = (SPSockUdp<ADDRESS_FAMILY_INET> *)ctx;
    ins->SendTo(fd, data, size, ip, port);
    sum++;
}

int main()
{
    SPSockUdp<ADDRESS_FAMILY_INET>::Config();

    auto ins = SPSockUdp<ADDRESS_FAMILY_INET>::GetInstance();

    if (ins->Bind(4567) == false)
        return -1;

    if (ins->SetCallback(echo_rcp, ins) == false)
        return -1;

    if (ins->SetSignalExit(SIGINT) == false)
        return -1;

    ins->EventLoop();
    ins->Release();
}