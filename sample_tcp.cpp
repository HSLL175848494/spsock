
#include "SPSock.hpp"
using namespace HSLL;

class EchoServer
{
    std::string waitSent;
    SOCKController controller;

    void Send()
    {
        unsigned int ptr = 0;
        while (ptr < waitSent.size())
        {
            std::string_view sv(waitSent.data() + ptr, waitSent.size() - ptr);
            ssize_t bytes = controller.Write(sv.data(), sv.size());

            if (bytes == 0)
            {
                break;
            }
            else if (bytes == -1)
            {
                controller.EnableEvent(false, false);
                return;
            }

            ptr += bytes;
        }

        waitSent = waitSent.substr(ptr);
        controller.EnableEvent(true, true);
    }

public:
    EchoServer(SOCKController controller) : controller(controller) {}

    void DealRead()
    {
        char buf[1024];
        ssize_t bytes;

        while ((bytes = controller.Read(buf, 1024)) > 0)
            waitSent.append(buf, bytes);

        if (bytes == -1)
        {
            controller.EnableEvent(false, false);
            return;
        }

        Send();
    }

    void DealWrite()
    {
        Send();
    }
};

void echo_rdp(void *ctx)
{
    EchoServer *ec = (EchoServer *)ctx;
    ec->DealRead();
}

void echo_wtp(void *ctx)
{
    EchoServer *ec = (EchoServer *)ctx;
    ec->DealWrite();
}

void echo_csp(void *ctx)
{
    delete (EchoServer *)ctx;
}

void *echo_cnp(SOCKController controller, const char *ip, unsigned short port)
{
    return new EchoServer(controller);
}

int main()
{
    auto ins = SPSockTcp<ADDRESS_FAMILY_INET>::GetInstance();

    if (ins->EnableKeepAlive(true, 120, 2, 10) != 0)
        return -1;
    if (ins->EnableLinger(true, 5) != 0)
        return -1;
    if (ins->SetCallback(echo_cnp, echo_csp, echo_rdp, echo_wtp) != 0)
        return -1;
    if (ins->SetSignalExit(SIGINT) != 0)
        return -1;
    if (ins->Listen(4567) != 0)
        return -1;

    ins->EventLoop();
    ins->Release(); //    SPSockTcp<ADDRESS_FAMILY_INET>::Release()
    return 0;
}