#include "SPSock.h"

namespace HSLL
{
    // Static member initialization
    unsigned int SOCKADDR_IN<ADDRESS_FAMILY_INET>::UDP_MAX_BSIZE = 65535;
    unsigned int SOCKADDR_IN<ADDRESS_FAMILY_INET6>::UDP_MAX_BSIZE = 65527;

    // SOCKADDR_IN Implementation
    void SOCKADDR_IN<ADDRESS_FAMILY_INET>::INIT(sockaddr_in &address, unsigned short port)
    {
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);
    }

    void SOCKADDR_IN<ADDRESS_FAMILY_INET6>::INIT(sockaddr_in6 &address, unsigned short port)
    {
        address.sin6_family = AF_INET6;
        address.sin6_addr = in6addr_any;
        address.sin6_port = htons(port);
    }

    // TCP Implementation
    template <ADDRESS_FAMILY address_family>
    void SPSockTcp<address_family>::SetLinger(int fd)
    {
        if (setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin)) != 0)
            HSLL_LOGINFO(LOG_LEVEL_WARNING, "setsockopt(SO_LINGER) failed: ", strerror(errno));
    }

    template <ADDRESS_FAMILY address_family>
    void SPSockTcp<address_family>::SetKeepAlive(int fd)
    {
        if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &alive.keepAlive, sizeof(alive.keepAlive)) != 0)
            HSLL_LOGINFO(LOG_LEVEL_WARNING, "setsockopt(SO_KEEPALIVE) failed: ", strerror(errno));

        if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &alive.aliveSeconds, sizeof(alive.aliveSeconds)) != 0)
            HSLL_LOGINFO(LOG_LEVEL_WARNING, "setsockopt(TCP_KEEPIDLE) failed: ", strerror(errno));

        if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &alive.detectTimes, sizeof(alive.detectTimes)) != 0)
            HSLL_LOGINFO(LOG_LEVEL_WARNING, "setsockopt(TCP_KEEPCNT) failed: ", strerror(errno));

        if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &alive.detectInterval, sizeof(alive.detectInterval)) != 0)
            HSLL_LOGINFO(LOG_LEVEL_WARNING, "setsockopt(TCP_KEEPINTVL) failed: ", strerror(errno));
    }

    template <ADDRESS_FAMILY address_family>
    bool SPSockTcp<address_family>::HandleConnect(int idlefd)
    {
        using SOCKADDR = SOCKADDR_IN<address_family>;
        typename SOCKADDR::TYPE addr;
        socklen_t addrlen = sizeof(addr);

        int fd = accept(listenfd, (sockaddr *)&addr, &addrlen);
        if (fd == -1)
        {
            if (errno == EMFILE)
            {
                close(idlefd);
                idlefd = accept(listenfd, NULL, NULL);
                close(idlefd);
                idlefd = open("/dev/null", O_RDONLY | O_CLOEXEC);
            }
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "accept() failed: ", strerror(errno));
            return false;
        }

        if (alive.keepAlive)
            SetKeepAlive(fd);

        if (lin.l_onoff)
            SetLinger(fd);

        int flags = fcntl(fd, F_GETFL);
        if (flags == -1)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "fcntl(F_GETFL) failed: ", strerror(errno));
            close(fd);
            return true;
        }

        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "fcntl(F_SETFL) failed: ", strerror(errno));
            close(fd);
            return true;
        }

        auto [it, success] = connections.emplace(fd, SOCKController{});
        if (!success)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "Failed to add connection to map");
            close(fd);
            return true;
        }

        auto &controller = it->second;
        if constexpr (address_family == ADDRESS_FAMILY_INET)
        {
            inet_ntop(AF_INET, &addr.sin_addr, controller.ip, INET6_ADDRSTRLEN);
            controller.port = ntohs(addr.sin_port);
        }
        else
        {
            inet_ntop(AF_INET6, &addr.sin6_addr, controller.ip, INET6_ADDRSTRLEN);
            controller.port = ntohs(addr.sin6_port);
        }

        void *ctx = nullptr;
        if (proc.cnp)
            ctx = proc.cnp(controller.ip, controller.port);

        IOThreadInfo *info = &loopInfo[0];

        for (int i = 1; i < loopInfo.size(); i++)
        {
            if (loopInfo[i].count < info->count)
                info = &loopInfo[i];
        }

        if (!controller.init(fd, ctx, info))
        {
            HSLL_LOGINFO(LOG_LEVEL_WARNING, "Insufficient memory space");
            CloseConnection(&controller);
        }
        else
        {
            epoll_event event;
            event.events = EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLONESHOT | configGlobal.EPOLL_DEFAULT_EVENT;
            event.data.fd = fd;
            if (epoll_ctl(info->epollfd, EPOLL_CTL_ADD, fd, &event) != 0)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "epoll_ctl(EPOLL_CTL_ADD) failed: ", strerror(errno));
                CloseConnection(&controller);
                return true;
            }
            info->count++;
            HSLL_LOGINFO(LOG_LEVEL_INFO, "Accepted new connection from: ", controller.ipPort);
        }
        return true;
    }

    template <ADDRESS_FAMILY address_family>
    void SPSockTcp<address_family>::HandleExit(int sg)
    {
        if (SPSockTcp<address_family>::exitFlag)
        {
            SPSockTcp<address_family>::exitFlag.store(false, std::memory_order_release);
            HSLL_LOGINFO_NOPREFIX(LOG_LEVEL_CRUCIAL, "")
            HSLL_LOGINFO(LOG_LEVEL_CRUCIAL, "Caught signal ", sg, ", exiting event loop");
        }
    }

    template <ADDRESS_FAMILY address_family>
    void SPSockTcp<address_family>::ActiveClose(SOCKController *controller)
    {
        epoll_ctl(controller->info->epollfd, EPOLL_CTL_DEL, controller->fd, nullptr);
        auto This = GetInstance();
        std::lock_guard<std::mutex> lock(This->cList.mtx);
        This->cList.connections.push_back(controller);
    }

    template <ADDRESS_FAMILY address_family>
    bool SPSockTcp<address_family>::EnableEvent(SOCKController *controller, bool read, bool write)
    {
        epoll_event event;
        event.data.fd = controller->fd;
        event.events = EPOLLERR | EPOLLRDHUP | EPOLLHUP | EPOLLONESHOT;

        if (read)
            event.events |= EPOLLIN;

        if (write)
            event.events |= EPOLLOUT;

        if (epoll_ctl(controller->info->epollfd, EPOLL_CTL_MOD, controller->fd, &event) != 0)
            return false;

        return true;
    }

    template <ADDRESS_FAMILY address_family>
    void SPSockTcp<address_family>::CloseConnection(SOCKController *controller)
    {
        if (proc.csp)
            proc.csp(controller);

        close(controller->fd);
        connections.erase(controller->fd);
    }

    template <ADDRESS_FAMILY address_family>
    bool SPSockTcp<address_family>::CalculateOptimalThreadCounts(int *ioThreads, int *workerThreads)
    {
        unsigned int hardware_threads = std::thread::hardware_concurrency();
        if (hardware_threads == 0)
            return false;

        if (hardware_threads <= 2)
        {
            *workerThreads = 1;
            *ioThreads = 1;
        }
        else
        {
            float weight = configGlobal.WORKER_THREAD_RATIO;
            *workerThreads = hardware_threads * weight + 0.5;
            *ioThreads = hardware_threads - *workerThreads;

            if (*workerThreads == 0)
            {
                (*workerThreads)++;
                (*ioThreads)--;
            }
            else if (*ioThreads == 0)
            {
                (*workerThreads)--;
                (*ioThreads)++;
            }
        }
        return true;
    }

    template <ADDRESS_FAMILY address_family>
    bool SPSockTcp<address_family>::CreateIOEventLoop(ThreadPool<SockTaskTcp> *pool, int num)
    {
        for (int i = 0; i < num; i++)
        {
            int epollfd, exitfd;

            if ((epollfd = epoll_create1(0)) == -1)
                break;

            if ((exitfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) == -1)
            {
                close(epollfd);
                break;
            }

            epoll_event event;
            event.data.fd = exitfd;
            event.events = EPOLLIN | EPOLLERR | EPOLLHUP;
            if (epoll_ctl(epollfd, EPOLL_CTL_ADD, exitfd, &event) != 0)
            {
                close(epollfd);
                close(exitfd);
                break;
            }

            loopInfo.push_back({0, epollfd, exitfd});
        }

        if (loopInfo.size() != num)
        {
            for (int i = 0; i < loopInfo.size(); i++)
            {
                close(loopInfo.at(i).epollfd);
                close(loopInfo.at(i).exitfd);
            }
            return false;
        }

        for (int i = 0; i < num; i++)
            loops.emplace_back(std::thread{&SPSockTcp<address_family>::IOEventLoop, this, pool, loopInfo.at(i).epollfd, loopInfo.at(i).exitfd});

        return true;
    }

    template <ADDRESS_FAMILY address_family>
    void SPSockTcp<address_family>::ExitIOEventLoop()
    {
        for (int i = 0; i < loops.size(); i++)
        {
            uint64_t value = 1;
            ssize_t bytes = write(loopInfo.at(i).exitfd, &value, sizeof(value));
        }

        for (int i = 0; i < loops.size(); i++)
        {
            loops.at(i).join();
            close(loopInfo.at(i).epollfd);
            close(loopInfo.at(i).exitfd);
        }
    }

    template <ADDRESS_FAMILY address_family>
    bool SPSockTcp<address_family>::MainEventLoop()
    {
        int idlefd;
        if ((idlefd = ::open("/dev/null", O_RDONLY | O_CLOEXEC)) == -1)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "open \"/dev/null\" error");
            return false;
        }

        pollfd fds[1];
        fds[0].fd = listenfd;
        fds[0].events = POLLIN;

        auto lastCloseListTime = std::chrono::steady_clock::now();

        while (exitFlag.load(std::memory_order_acquire))
        {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCloseListTime);

            int timeout = 50 - elapsed.count();
            if (timeout < 0)
                timeout = 0;

            int ret = poll(fds, 1, timeout);
            if (ret == -1)
            {
                if (errno == EINTR)
                    continue;

                close(idlefd);
                return false;
            }

            now = std::chrono::steady_clock::now();
            elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCloseListTime);

            if (elapsed.count() >= 50)
            {
                HandleCloseList();
                lastCloseListTime = now;
            }

            if (ret > 0 && (!HandleConnect(idlefd)))
            {
                close(idlefd);
                return false;
            }
        }

        close(idlefd);
        return true;
    }

    template <ADDRESS_FAMILY address_family>
    void SPSockTcp<address_family>::IOEventLoop(ThreadPool<SockTaskTcp> *pool, int epollfd, int exitfd)
    {
        UtilTask utilTask;
        if (!utilTask.init(pool))
        {
            throw std::bad_alloc();
            return;
        }

        epoll_event *events = new (std::nothrow) epoll_event[configGlobal.EPOLL_MAX_EVENT_BSIZE];
        if (!events)
        {
            throw std::bad_alloc();
            return;
        }

        while (true)
        {
            int nfds = epoll_wait(epollfd, events, configGlobal.EPOLL_MAX_EVENT_BSIZE, -1);
            if (nfds == -1)
            {
                if (errno == EINTR)
                    continue;

                delete[] events;
                throw strerror(errno);
                break;
            }

            for (int i = 0; i < nfds; i++)
            {
                int fd = events[i].data.fd;

                if (fd == exitfd)
                {
                    delete[] events;
                    return;
                }

                if (events[i].events & (EPOLLHUP | EPOLLERR))
                {
                    ActiveClose(&connections.find(fd)->second);
                }
                else if (events[i].events & (EPOLLIN | EPOLLRDHUP))
                {
                    auto it = connections.find(fd);

                    if (events[i].events & EPOLLRDHUP)
                        it->second.peerClosed = true;

                    if (!HandleRead(&it->second, &utilTask))
                        ActiveClose(&it->second);
                }
                else if (events[i].events & EPOLLOUT)
                {
                    auto it = connections.find(fd);

                    if (!HandleWrite(&it->second, &utilTask))
                        ActiveClose(&it->second);
                }
            }

            utilTask.reset();
        }
    }

    template <ADDRESS_FAMILY address_family>
    void SPSockTcp<address_family>::Cleanup()
    {
        if (connections.size())
            HSLL_LOGINFO(LOG_LEVEL_WARNING, "Cleaning up unclosed connections");

        for (auto &it : connections)
        {
            if (proc.csp)
                proc.csp(&it.second);

            close(it.first);
            HSLL_LOGINFO(LOG_LEVEL_INFO, "Connection force closed : ", it.second.ipPort);
        }
        connections.clear();
    }

    template <ADDRESS_FAMILY address_family>
    SPSockTcp<address_family>::SPSockTcp() : listenfd(-1), status(0), lin{0, 0}, alive{0, 0, 0, 0} {}

    template <ADDRESS_FAMILY address_family>
    SPSockTcp<address_family>::~SPSockTcp()
    {
        if (listenfd != -1)
            close(listenfd);
    };

    template <ADDRESS_FAMILY address_family>
    void SPSockTcp<address_family>::Config(SPConfig config)
    {
        assert(config.READ_BSIZE >= 1024 && (config.READ_BSIZE % 1024) == 0);
        assert(config.WRITE_BSIZE >= 1024 && (config.WRITE_BSIZE % 1024) == 0);
        assert(config.BUFFER_POOL_PEER_ALLOC_NUM >= 1 && config.BUFFER_POOL_PEER_ALLOC_NUM <= 1024);
        assert(config.BUFFER_POOL_MIN_BLOCK_NUM >= config.BUFFER_POOL_PEER_ALLOC_NUM);
        assert(config.EPOLL_MAX_EVENT_BSIZE > 0 && config.EPOLL_MAX_EVENT_BSIZE <= 65535);
        assert((config.EPOLL_DEFAULT_EVENT & ~(EPOLLIN | EPOLLOUT)) == 0);
        assert(config.THREADPOOL_QUEUE_LENGTH > 0 && config.THREADPOOL_QUEUE_LENGTH <= 1048576);
        assert(config.THREADPOOL_BATCH_SIZE_SUBMIT > 0 && config.THREADPOOL_BATCH_SIZE_SUBMIT <= config.THREADPOOL_QUEUE_LENGTH);
        assert(config.THREADPOOL_BATCH_SIZE_PROCESS > 0 && config.THREADPOOL_BATCH_SIZE_PROCESS <= 1024);
        assert(config.WORKER_THREAD_RATIO > 0.0 && config.WORKER_THREAD_RATIO < 1.0);

        funcClose = ActiveClose;
        funcEvent = EnableEvent;
        configGlobal = config;
    }

    template <ADDRESS_FAMILY address_family>
    SPSockTcp<address_family> *SPSockTcp<address_family>::GetInstance()
    {
        if (instance == nullptr)
            instance = new (std::nothrow) SPSockTcp;

        return instance;
    }

    template <ADDRESS_FAMILY address_family>
    bool SPSockTcp<address_family>::Listen(unsigned short port)
    {
        if ((status & 0x1) == 0x1)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "Listen() cannot be called multiple times");
            return false;
        }

        using SOCKADDR = SOCKADDR_IN<address_family>;
        typename SOCKADDR::TYPE addr;

        if ((listenfd = socket(address_family, PROTOCOL_TCP, 0)) == -1)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "socket() failed: ", strerror(errno));
            return false;
        }

        int reuse = 1;
        if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1 ||
            setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) == -1)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "setsockopt(SO_REUSEPORT|SO_REUSEADDR) failed: ", strerror(errno));
            close(listenfd);
            listenfd = -1;
            return false;
        }

        SOCKADDR::INIT(addr, port);
        if (bind(listenfd, (sockaddr *)&addr, sizeof(addr)) == -1)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "bind() failed: ", strerror(errno));
            close(listenfd);
            listenfd = -1;
            return false;
        }

        if (listen(listenfd, SOMAXCONN) == -1)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "listen() failed: ", strerror(errno));
            close(listenfd);
            listenfd = -1;
            return false;
        }

        status |= 0x1;
        HSLL_LOGINFO(LOG_LEVEL_INFO, "Started listening on port: ", port);
        return true;
    }

    template <ADDRESS_FAMILY address_family>
    void SPSockTcp<address_family>::HandleCloseList()
    {
        std::unique_lock<std::mutex> lock(cList.mtx);
        if (cList.connections.size() > 10)
        {
            auto connections = std::move(cList.connections);
            lock.unlock();

            for (auto &&controller : connections)
            {
                HSLL_LOGINFO(LOG_LEVEL_INFO, "Connection closed: ", controller->ipPort);
                CloseConnection(controller);
                controller->info->count--;
            }
        }
        else
        {
            for (auto &&controller : cList.connections)
            {
                HSLL_LOGINFO(LOG_LEVEL_INFO, "Connection closed: ", controller->ipPort);
                CloseConnection(controller);
                controller->info->count--;
            }
            cList.connections.clear();
        }
    }

    template <ADDRESS_FAMILY address_family>
    bool SPSockTcp<address_family>::HandleRead(SOCKController *controller, UtilTask *utilTask)
    {
        if (proc.rdp)
        {
            if (!controller->readSocket())
                return false;

            if (controller->isPeerClosed() && controller->getReadBufferSize() == 0)
                return false;

            if (markGlobal.readMark == 0)
            {
                utilTask->append(controller, proc.rdp);
                return true;
            }

            if (controller->getReadBufferSize() >= markGlobal.readMark)
            {
                utilTask->append(controller, proc.rdp);
                return true;
            }
            else if (!controller->renableEvents())
            {
                return false;
            }
        }
        else if (proc.wtp)
        {
            if (controller->enableEvents(false, true))
                return false;
        }
        else
        {
            return false;
        }
        return true;
    }

    template <ADDRESS_FAMILY address_family>
    bool SPSockTcp<address_family>::HandleWrite(SOCKController *controller, UtilTask *utilTask)
    {
        if (proc.wtp)
        {
            if (controller->isPeerClosed() && controller->getReadBufferSize() == 0)
                return false;

            if (markGlobal.writeMark == 0xffffffff)
            {
                utilTask->append(controller, proc.wtp);
                return true;
            }

            if (controller->commitWrite() == -1)
                return false;

            if (controller->getWriteBufferSize() <= markGlobal.writeMark)
            {
                utilTask->append(controller, proc.wtp);
                return true;
            }
            else if (!controller->renableEvents())
            {
                return false;
            }
        }
        else if (proc.rdp)
        {
            if (controller->enableEvents(true, false))
                return false;
        }
        else
        {
            return false;
        }
        return true;
    }

    template <ADDRESS_FAMILY address_family>
    bool SPSockTcp<address_family>::EventLoop()
    {
        if ((status & 0x8) == 0x8)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "EventLoop() cannot be called multiple times");
            return false;
        }

        if ((status & 0x1) != 0x1)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "Listen() not called");
            return false;
        }

        if ((status & 0x2) != 0x2)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "SetCallback() not called");
            return false;
        }

        if ((status & 0x4) != 0x4)
            HSLL_LOGINFO(LOG_LEVEL_WARNING, "Exit signal handler not configured");

        int ioThreads, workerThreads;
        if (!CalculateOptimalThreadCounts(&ioThreads, &workerThreads))
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "Failed to get the number of CPU cores");
            return false;
        }

        ThreadPool<SockTaskTcp> pool;

        if (!pool.init(configGlobal.THREADPOOL_QUEUE_LENGTH, workerThreads,
                       configGlobal.THREADPOOL_BATCH_SIZE_PROCESS))
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "Failed to initialize thread pool: There is not enough memory space");
            return false;
        }

        if (!CreateIOEventLoop(&pool, ioThreads))
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "CreateIOEventLoop() failed");
            return false;
        }

        HSLL_LOGINFO(LOG_LEVEL_CRUCIAL, "Event loop start");

        if (!MainEventLoop())
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "MainEventLoop() failed");

        pool.exit();
        ExitIOEventLoop();

        status |= 0x8;
        HSLL_LOGINFO(LOG_LEVEL_CRUCIAL, "Event loop exited");
        return true;
    }

    template <ADDRESS_FAMILY address_family>
    bool SPSockTcp<address_family>::EnableLinger(bool enable, int waitSeconds)
    {
        if (!enable)
        {
            lin.l_onoff = 0;
            return true;
        }

        if (waitSeconds <= 0)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "Invalid parameter: waitSeconds must be positive");
            return false;
        }

        lin = {1, waitSeconds};
        HSLL_LOGINFO(LOG_LEVEL_INFO, "Linger ", enable ? "enabled" : "disabled");
        return true;
    }

    template <ADDRESS_FAMILY address_family>
    bool SPSockTcp<address_family>::EnableKeepAlive(bool enable, int aliveSeconds, int detectTimes, int detectInterval)
    {
        if (!enable)
        {
            alive.keepAlive = 0;
            return true;
        }

        if (aliveSeconds <= 0 || detectTimes <= 0 || detectInterval <= 0)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "Invalid parameter: aliveSeconds, detectTimes, and detectInterval must be positive");
            return false;
        }

        alive = {1, aliveSeconds, detectTimes, detectInterval};
        HSLL_LOGINFO(LOG_LEVEL_INFO, "Keep-alive ", enable ? "enabled" : "disabled");
        return true;
    }

    template <ADDRESS_FAMILY address_family>
    bool SPSockTcp<address_family>::SetCallback(ConnectProc cnp, CloseProc csp, ReadProc rdp, WriteProc wtp)
    {
        if (cnp == nullptr && csp == nullptr && rdp == nullptr && wtp == nullptr)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "Invalid parameter: Parameters cannot be nullptr at the same time");
            return false;
        }
        proc = {rdp, wtp, cnp, csp};
        status |= 0x2;
        HSLL_LOGINFO(LOG_LEVEL_INFO, "Callbacks configured successfully");
        return true;
    }

    template <ADDRESS_FAMILY address_family>
    void SPSockTcp<address_family>::SetWaterMark(unsigned int readMark, unsigned int writeMark)
    {
        markGlobal = {readMark, writeMark};
        HSLL_LOGINFO(LOG_LEVEL_INFO, "Low water mark configured successfully");
    }

    template <ADDRESS_FAMILY address_family>
    bool SPSockTcp<address_family>::SetSignalExit(int sg)
    {
        if ((status & 0x4) == 0x4)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "SetSignalExit() cannot be called multiple times");
            return false;
        }

        struct sigaction sa;
        sa.sa_handler = HandleExit;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;

        if (sigaction(sg, &sa, nullptr) == -1)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "sigaction() failed: ", strerror(errno));
            return false;
        }

        status |= 0x4;
        HSLL_LOGINFO(LOG_LEVEL_INFO, "Exit signal handler configured for signal: ", sg);
        return true;
    }

    template <ADDRESS_FAMILY address_family>
    void SPSockTcp<address_family>::SetExitFlag()
    {
        exitFlag.store(false, std::memory_order_release);
    }

    template <ADDRESS_FAMILY address_family>
    void SPSockTcp<address_family>::Release()
    {
        if (instance)
        {
            delete instance;
            instance = nullptr;
            SPBufferPool::reset();
            HSLL_LOGINFO(LOG_LEVEL_INFO, "Instance released successfully");
        }
    }

    // UDP Implementation
    template <ADDRESS_FAMILY address_family>
    void SPSockUdp<address_family>::HandleExit(int sg)
    {
        if (SPSockUdp<address_family>::exitFlag)
        {
            HSLL_LOGINFO_NOPREFIX(LOG_LEVEL_CRUCIAL, "")
            HSLL_LOGINFO(LOG_LEVEL_CRUCIAL, "Caught signal ", sg, ", exiting event loop");
            SPSockUdp<address_family>::exitFlag.store(false, std::memory_order_release);
        }
    }

    template <ADDRESS_FAMILY address_family>
    SPSockUdp<address_family>::SPSockUdp() : sockfd(-1), status(0) {}

    template <ADDRESS_FAMILY address_family>
    SPSockUdp<address_family>::~SPSockUdp()
    {
        if (sockfd != -1)
            close(sockfd);
    }

    template <ADDRESS_FAMILY address_family>
    void SPSockUdp<address_family>::Config(LOG_LEVEL minlevel)
    {
        configGlobal.MIN_LOG_LEVEL = minlevel;
    };

    template <ADDRESS_FAMILY address_family>
    SPSockUdp<address_family> *SPSockUdp<address_family>::GetInstance()
    {
        if (instance == nullptr)
            instance = new (std::nothrow) SPSockUdp;

        return instance;
    }

    template <ADDRESS_FAMILY address_family>
    bool SPSockUdp<address_family>::Bind(unsigned short port)
    {
        if ((status & 0x1) == 0x1)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "Bind() cannot be called multiple times");
            return false;
        }

        using SOCKADDR = SOCKADDR_IN<address_family>;
        typename SOCKADDR::TYPE addr;

        if ((sockfd = socket(address_family, PROTOCOL_UDP, 0)) == -1)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "socket() failed: ", strerror(errno));
            return false;
        }

        int opt = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "setsockopt(SO_REUSEADDR) failed: ", strerror(errno));
            close(sockfd);
            sockfd = -1;
            return false;
        }

        SOCKADDR::INIT(addr, port);
        if (bind(sockfd, (sockaddr *)&addr, sizeof(addr)) == -1)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "bind() failed: ", strerror(errno));
            close(sockfd);
            sockfd = -1;
            return false;
        }

        status |= 0x1;
        HSLL_LOGINFO(LOG_LEVEL_INFO, "Bound to port: ", port, " successfully");
        return true;
    }

    template <ADDRESS_FAMILY address_family>
    bool SPSockUdp<address_family>::EventLoop()
    {
        if ((status & 0x8) == 0x8)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "EventLoop() cannot be called multiple times");
            return false;
        }

        if ((status & 0x1) != 0x1)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "Bind() not called");
            return false;
        }

        if ((status & 0x2) != 0x2)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "SetCallback() not called");
            return false;
        }

        if ((status & 0x4) != 0x4)
        {
            HSLL_LOGINFO(LOG_LEVEL_WARNING, "Exit signal handler not configured");
        }

        auto UDP_MAX_BSIZE = SOCKADDR_IN<address_family>::UDP_MAX_BSIZE;
        typename SOCKADDR_IN<address_family>::TYPE addr;
        char buf[UDP_MAX_BSIZE];
        socklen_t addrlen = sizeof(addr);

        HSLL_LOGINFO(LOG_LEVEL_CRUCIAL, "Event loop started");
        while (exitFlag.load(std::memory_order_acquire))
        {
            ssize_t bytes = recvfrom(sockfd, buf, UDP_MAX_BSIZE, 0, (sockaddr *)&addr, &addrlen);

            if (bytes == -1)
            {
                if (errno == EINTR)
                    continue;

                HSLL_LOGINFO(LOG_LEVEL_ERROR, "recvfrom() failed: ", strerror(errno));
                status |= 0x8;
                return false;
            }

            char ip[INET6_ADDRSTRLEN];
            unsigned short port = 0;

            if constexpr (address_family == ADDRESS_FAMILY_INET)
            {
                inet_ntop(AF_INET, &addr.sin_addr, ip, INET6_ADDRSTRLEN);
                port = ntohs(addr.sin_port);
            }
            else
            {
                inet_ntop(AF_INET6, &addr.sin6_addr, ip, INET6_ADDRSTRLEN);
                port = ntohs(addr.sin6_port);
            }

            rcp(ctx, buf, bytes, ip, port);
        }

        status |= 0x8;
        HSLL_LOGINFO(LOG_LEVEL_CRUCIAL, "Event loop exited");
        return true;
    }

    template <ADDRESS_FAMILY address_family>
    bool SPSockUdp<address_family>::SendTo(const void *data, size_t size, const char *ip, unsigned short port)
    {
        if ((status & 0x1) != 0x1)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "Bind() not called");
            return false;
        }

        if (data == nullptr || ip == nullptr || size == 0 || port == 0)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "Invalid parameter");
            return false;
        }

        using SOCKADDR = SOCKADDR_IN<address_family>;
        typename SOCKADDR::TYPE addr;

        if constexpr (address_family == ADDRESS_FAMILY_INET)
        {
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "inet_pton() failed: invalid address");
                return false;
            }
        }
        else
        {
            addr.sin6_family = AF_INET6;
            addr.sin6_port = htons(port);
            if (inet_pton(AF_INET6, ip, &addr.sin6_addr) <= 0)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "inet_pton() failed: invalid address");
                return false;
            }
        }

        if (sendto(sockfd, data, size, 0, (sockaddr *)&addr, sizeof(addr)) != size)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "sendto() failed: ", strerror(errno));
            return false;
        }
        return true;
    }

    template <ADDRESS_FAMILY address_family>
    bool SPSockUdp<address_family>::SetSignalExit(int sg)
    {
        struct sigaction sa;
        sa.sa_handler = HandleExit;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;

        if (sigaction(sg, &sa, nullptr) == -1)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "sigaction() failed: ", strerror(errno));
            return false;
        }

        status |= 0x4;
        HSLL_LOGINFO(LOG_LEVEL_INFO, "Exit signal handler configured for signal: ", sg);
        return true;
    }

    template <ADDRESS_FAMILY address_family>
    bool SPSockUdp<address_family>::SetCallback(RecvProc rcp, void *ctx)
    {
        if (rcp == nullptr)
        {
            HSLL_LOGINFO(LOG_LEVEL_ERROR, "Invalid parameter: RecvProc cannot be nullptr");
            return false;
        }

        this->rcp = rcp;
        this->ctx = ctx;
        status |= 0x2;
        HSLL_LOGINFO(LOG_LEVEL_INFO, "Callback configured successfully");
        return true;
    }

    template <ADDRESS_FAMILY address_family>
    void SPSockUdp<address_family>::SetExitFlag()
    {
        exitFlag.store(false, std::memory_order_release);
    }

    template <ADDRESS_FAMILY address_family>
    void SPSockUdp<address_family>::Release()
    {
        if (instance)
        {
            delete instance;
            instance = nullptr;
        }

        HSLL_LOGINFO(LOG_LEVEL_INFO, "Instance released successfully");
    }

    /* Static member initialization */
    template <ADDRESS_FAMILY address_family>
    std::atomic<bool> SPSockTcp<address_family>::exitFlag = true;

    template <ADDRESS_FAMILY address_family>
    std::atomic<bool> SPSockUdp<address_family>::exitFlag = true;

    template <ADDRESS_FAMILY address_family>
    SPSockTcp<address_family> *SPSockTcp<address_family>::instance = nullptr;

    template <ADDRESS_FAMILY address_family>
    SPSockUdp<address_family> *SPSockUdp<address_family>::instance = nullptr;

    // Explicit template instantiation
    template class SPSockTcp<ADDRESS_FAMILY_INET>;
    template class SPSockTcp<ADDRESS_FAMILY_INET6>;
    template class SPSockUdp<ADDRESS_FAMILY_INET>;
    template class SPSockUdp<ADDRESS_FAMILY_INET6>;
}