
#include "SPSock.hpp"
using namespace HSLL;

class EchoServer
{
    std::string waitSent;

    void Send(SOCKController *controller)
    {
        unsigned int ptr = 0;
        while (ptr < waitSent.size())
        {
            std::string_view sv(waitSent.data() + ptr, waitSent.size() - ptr);
            ssize_t bytes = controller->writeTemp(sv.data(), sv.size());

            if (bytes == 0)
            {
                break;
            }
            else if (bytes == -1)
            {
                controller->enableEvents(false, false);
                return;
            }

            ptr += bytes;
        }

        waitSent = waitSent.substr(ptr);

        if (controller->commitWrite())
            controller->enableEvents(true, true);
        else
            controller->enableEvents(true, false);
    }

public:
    void DealRead(SOCKController *controller)
    {
        char buf[1024];
        ssize_t bytes;

        while ((bytes = controller->read(buf, 1024)) > 0)
            waitSent.append(buf, bytes);

        if (bytes == -1)
        {
            controller->enableEvents(false, false);
            return;
        }

        Send(controller);
    }

    void DealWrite(SOCKController *controller)
    {
        Send(controller);
    }
};

void echo_rdp(SOCKController *controller)
{
    EchoServer *ec = (EchoServer *)controller->getCtx();
    ec->DealRead(controller);
}

void echo_wtp(SOCKController *controller)
{
    EchoServer *ec = (EchoServer *)controller->getCtx();
    ec->DealWrite(controller);
}

void echo_csp(SOCKController *controller)
{
    delete (EchoServer *)controller->getCtx();
}

void *echo_cnp(const char *ip, unsigned short port)
{
    return new EchoServer();
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