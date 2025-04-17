#include "SPSock.hpp"

using namespace HSLL;

void echo_rcp(void *ctx, const char *data, ssize_t size, const char *ip, unsigned short port)
{
    auto ins = (SPSockUdp<ADDRESS_FAMILY_INET> *)ctx;
    ins->SendTo(data, size, ip, port);
}

int main()
{
    auto ins = SPSockUdp<ADDRESS_FAMILY_INET>::GetInstance();

    if (ins->Bind(4567) != 0)
        return -1;

    if (ins->SetCallback(echo_rcp, ins))
        return -1;

    if (ins->SetSignalExit(SIGINT) != 0)
        return -1;

    ins->EventLoop();
    ins->Release(); //    SPSockUdp<ADDRESS_FAMILY_INET>::Release()
}