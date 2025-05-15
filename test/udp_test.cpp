#include "MemoryTracer.h"
#include "../SPSock.h"

using namespace HSLL;

std::atomic<unsigned int> sum = 0;

void echo_rcp(void *ctx, int fd, const char *data, size_t size, const char *ip, unsigned short port)
{
    auto ins = (SPSockUdp<ADDRESS_FAMILY_INET> *)ctx;
    ins->SendTo(fd, data, size, ip, port);
    sum++;
}

int main() // g++ -o3  ../*.cpp udp_test.cpp -o test
{
    HSLL::TRACING::StartTracing();

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

    HSLL_LOGINFO(LOG_LEVEL_CRUCIAL, "There are ", HSLL::TRACING::GetAllocSize(), " bytes not yet released");
    HSLL_LOGINFO(LOG_LEVEL_CRUCIAL, "There are ", sum.load(), " package");
}