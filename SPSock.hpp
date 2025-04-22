#ifndef HSLL_SPSOCK
#define HSLL_SPSOCK

#include <list>
#include <mutex>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/tcp.h>
#include <unordered_map>
#include <unordered_set>

#include "SPLog.hpp"
#include "noncopyable.hpp"
#include "SPInitializer.hpp"

namespace HSLL
{
    /**
     * @brief Template structure for socket address initialization
     * @tparam address_family IP version specification (IPv4/IPv6)
     */
    template <ADDRESS_FAMILY address_family>
    struct SOCKADDR_IN;

    /**
     * @brief Specialization for IPv4 address family
     */
    template <>
    struct SOCKADDR_IN<ADDRESS_FAMILY_INET>
    {
        using TYPE = sockaddr_in; ///< Underlying socket address type

        static unsigned int UDP_MAX_BSIZE; ///< Max buffer size for UDP

        /**
         * @brief Initializes IPv4 socket address structure
         * @param address Reference to sockaddr_in structure
         * @param port Network byte order port number
         */
        static void INIT(sockaddr_in &address, unsigned short port)
        {
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = INADDR_ANY;
            address.sin_port = htons(port);
        }
    };

    /**
     * @brief Specialization for IPv6 address family
     */
    template <>
    struct SOCKADDR_IN<ADDRESS_FAMILY_INET6>
    {
        using TYPE = sockaddr_in6; ///< Underlying socket address type

        static unsigned int UDP_MAX_BSIZE; ///< Max buffer size for UDP

        /**
         * @brief Initializes IPv6 socket address structure
         * @param address Reference to sockaddr_in6 structure
         * @param port Network byte order port number
         */
        static void INIT(sockaddr_in6 &address, unsigned short port)
        {
            address.sin6_family = AF_INET6;
            address.sin6_addr = in6addr_any;
            address.sin6_port = htons(port);
        }
    };

    /**
     * @brief Main TCP socket management class
     * @tparam address_family IP version (IPv4/IPv6)
     */
    template <ADDRESS_FAMILY address_family = ADDRESS_FAMILY::ADDRESS_FAMILY_INET>
    class SPSockTcp : noncopyable
    {
        linger lin;        ///< Linger configuration
        SPSockProc proc;   ///< User connections callbacks
        SPSockAlive alive; ///< Keep-alive settings

        CloseList list;                                      ///< Connection close list
        std::unordered_map<int, SOCKController> connections; ///< Active connections

        int status;   ///< Internal status flags
        int idlefd;   ///< Idle file descriptor
        int epollfd;  ///< Epoll file descriptor
        int listenfd; ///< Listening socket descriptor

        static void *exitCtx;                       ///< Event loop exit ctx
        static ExitProc exitProc;                   ///< Event loop exit proc
        static std::atomic<bool> exitFlag;          ///< Event loop control flag
        static SPSockTcp<address_family> *instance; ///< Singleton instance

        /**
         * @brief Configures socket linger options
         * @param fd Socket descriptor to configure
         */
        void SetLinger(int fd)
        {
            if (setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin)) != 0)
            {
                HSLL_LOGINFO(LOG_LEVEL_WARNING, "setsockopt(SO_LINGER) failed: ", strerror(errno));
            }
        }

        /**
         * @brief Configures TCP keep-alive options
         * @param fd Socket descriptor to configure
         */
        void SetKeepAlive(int fd)
        {
            if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &alive.keepAlive, sizeof(alive.keepAlive)) != 0)
            {
                HSLL_LOGINFO(LOG_LEVEL_WARNING, "setsockopt(SO_KEEPALIVE) failed: ", strerror(errno));
            }
            if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &alive.aliveSeconds, sizeof(alive.aliveSeconds)) != 0)
            {
                HSLL_LOGINFO(LOG_LEVEL_WARNING, "setsockopt(TCP_KEEPIDLE) failed: ", strerror(errno));
            }
            if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &alive.detectTimes, sizeof(alive.detectTimes)) != 0)
            {
                HSLL_LOGINFO(LOG_LEVEL_WARNING, "setsockopt(TCP_KEEPCNT) failed: ", strerror(errno));
            }
            if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &alive.detectInterval, sizeof(alive.detectInterval)) != 0)
            {
                HSLL_LOGINFO(LOG_LEVEL_WARNING, "setsockopt(TCP_KEEPINTVL) failed: ", strerror(errno));
            }
        }

        /**
         * @brief Handles new incoming connections
         */
        void DealConnect()
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
                return;
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
                return;
            }

            if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "fcntl(F_SETFL) failed: ", strerror(errno));
                close(fd);
                return;
            }

            epoll_event event;
            event.events = EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLONESHOT | SPSOCK_EPOLL_DEFAULT_EVENT;
            event.data.fd = fd;
            if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event) != 0)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "epoll_ctl(EPOLL_CTL_ADD) failed: ", strerror(errno));
                close(fd);
                return;
            }

            auto [it, success] = connections.emplace(fd, SOCKController{});
            if (!success)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "Failed to add connection to map");
                close(fd);
                return;
            }

            auto &sockController = it->second;
            if constexpr (address_family == ADDRESS_FAMILY_INET)
            {
                inet_ntop(AF_INET, &addr.sin_addr, sockController.ip, INET6_ADDRSTRLEN);
                sockController.port = ntohs(addr.sin_port);
            }
            else
            {
                inet_ntop(AF_INET6, &addr.sin6_addr, sockController.ip, INET6_ADDRSTRLEN);
                sockController.port = ntohs(addr.sin6_port);
            }

            void *ctx = proc.cnp(sockController.ip, sockController.port);
            sockController.init(fd, ctx, FuncClose, FuncEnableEvent, SPSOCK_READ_BSIZE, SPSOCK_WRITE_BSIZE, SPSOCK_EPOLL_DEFAULT_EVENT);
            HSLL_LOGINFO(LOG_LEVEL_INFO, "Accepted new connection from: ", sockController.ipPort);
        }

        /**
         * @brief Signal handler for graceful shutdown
         */
        static void DealExit(int sg)
        {
            if (SPSockTcp<address_family>::exitFlag)
            {
                HSLL_LOGINFO_NOPREFIX(LOG_LEVEL_CRUCIAL, "")
                HSLL_LOGINFO(LOG_LEVEL_CRUCIAL, "Caught signal ", sg, ", exiting event loop");
                if (SPSockTcp<address_family>::exitProc)
                    SPSockTcp<address_family>::exitProc(SPSockTcp<address_family>::exitCtx);
                SPSockTcp<address_family>::exitFlag.store(false, std::memory_order_release);
            }
        }

        /**
         * @brief Close callback implementation
         * @param fd File descriptor to close
         */
        static void FuncClose(int fd)
        {
            auto This = GetInstance();
            std::lock_guard<std::mutex> lock(This->list.mtx);
            This->list.connections.push_back(fd);
        }

        /**
         * @brief Event control callback implementation
         * @param fd File descriptor to modify
         * @param read Enable read events
         * @param write Enable write events
         * @return true on success, false on failure
         */
        static bool FuncEnableEvent(int fd, bool read, bool write)
        {
            auto This = GetInstance();
            epoll_event event;
            event.data.fd = fd;
            event.events = EPOLLERR | EPOLLHUP | EPOLLONESHOT;

            if (read)
                event.events |= EPOLLIN;

            if (write)
                event.events |= EPOLLOUT;

            if (epoll_ctl(This->epollfd, EPOLL_CTL_MOD, fd, &event) != 0)
                return false;

            return true;
        }

        /**
         * @brief Closes a connection and cleans up resources
         * @param fd File descriptor of the connection to close
         * @return Connection information string
         */
        std::string CloseConnection(int fd)
        {
            if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr) != 0)
                HSLL_LOGINFO(LOG_LEVEL_WARNING, "epoll_ctl(EPOLL_CTL_DEL) failed: ", strerror(errno));

            auto it = connections.find(fd);
            auto &sockController = it->second;
            std::string info = std::move(sockController.ipPort);

            if (proc.csp)
                proc.csp(&sockController);

            connections.erase(it);
            close(fd);
            return info;
        }

        /**
         * @brief Cleans up all connections and resources
         */
        void Clean()
        {
            if (listenfd != -1)
                close(listenfd);

            if ((status & 0x8) == 0x8)
            {
                for (auto &it : connections)
                {
                    if (epoll_ctl(epollfd, EPOLL_CTL_DEL, it.first, nullptr) != 0)
                        HSLL_LOGINFO(LOG_LEVEL_WARNING, "epoll_ctl(EPOLL_CTL_DEL) failed: ", strerror(errno));

                    if (proc.csp)
                        proc.csp(&it.second);

                    close(it.first);
                    HSLL_LOGINFO(LOG_LEVEL_INFO, "Closed connection: ", it.second.ipPort);
                }

                close(idlefd);
                close(epollfd);
                connections.clear();
            }
        }

        /**
         * @brief Private constructor
         */
        SPSockTcp() : epollfd(-1), listenfd(-1), idlefd(-1), status(0), lin{0, 0}, alive{1, 120, 3, 10} {};

    public:
        /**
         * @brief Gets singleton instance
         * @note Not thread-safe
         * @return Pointer to the singleton instance
         */
        static SPSockTcp *GetInstance()
        {
            if (instance == nullptr)
                instance = new SPSockTcp;

            return instance;
        }

        /**
         * @brief Starts listening on specified port
         * @param port Port number to listen on
         * @return 0 on success, error code on failure
         * @note One-time call function
         */
        int Listen(unsigned short port) SPSOCK_ONE_TIME_CALL
        {
            if ((status & 0x1) == 0x1)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "Listen() cannot be called multiple times");
                return 15;
            }

            using SOCKADDR = SOCKADDR_IN<address_family>;
            typename SOCKADDR::TYPE addr;

            if ((listenfd = socket(address_family, PROTOCOL_TCP, 0)) == -1)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "socket() failed: ", strerror(errno));
                return 2;
            }

            int reuse = 1;
            if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1 ||
                setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) == -1)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "setsockopt(SO_REUSEPORT|SO_REUSEADDR) failed: ", strerror(errno));
                close(listenfd);
                listenfd = -1;
                return 3;
            }

            SOCKADDR::INIT(addr, port);
            if (bind(listenfd, (sockaddr *)&addr, sizeof(addr)) == -1)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "bind() failed: ", strerror(errno));
                close(listenfd);
                listenfd = -1;
                return 4;
            }

            if (listen(listenfd, SOMAXCONN) == -1)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "listen() failed: ", strerror(errno));
                close(listenfd);
                listenfd = -1;
                return 5;
            }

            status |= 0x1;
            HSLL_LOGINFO(LOG_LEVEL_INFO, "Started listening on port: ", port);
            return 0;
        }

        /**
         * @brief Main event processing loop
         * @param policy Full load strategy,wait or abandon
         * @return 0 on success, error code on failure
         * @note One-time call function
         */
        int EventLoop(FULL_LOAD_POLICY policy = FULL_LOAD_POLICY_DISCARD) SPSOCK_ONE_TIME_CALL
        {
            if ((status & 0x8) == 0x8)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "EventLoop() cannot be called multiple times");
                return 15;
            }

            if ((status & 0x1) != 0x1)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "Listen() not called");
                return 13;
            }

            if ((status & 0x2) != 0x2)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "SetCallback() not called");
                return 14;
            }

            if ((status & 0x4) != 0x4)
                HSLL_LOGINFO(LOG_LEVEL_WARNING, "Exit signal handler not configured");

            if ((idlefd = ::open("/dev/null", O_RDONLY | O_CLOEXEC)) == -1)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "open \"/dev/null\" error");
                return 18;
            }

            epoll_event event;
            epoll_event events[SPSOCK_MAX_EVENT_BSIZE];
            if ((epollfd = epoll_create1(0)) == -1)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "epoll_create1() failed: ", strerror(errno));
                return 9;
            }

            event.data.fd = listenfd;
            event.events = EPOLLIN | EPOLLERR | EPOLLHUP;
            if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &event) != 0)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "epoll_ctl(EPOLL_CTL_ADD) failed: ", strerror(errno));
                close(epollfd);
                epollfd = -1;
                return 10;
            }

            long cores = sysconf(_SC_NPROCESSORS_ONLN);
            if (cores == -1)
            {
                cores = SPSOCK_THREADPOOL_DEFAULT_THREADS_NUM;
                HSLL_LOGINFO(LOG_LEVEL_WARNING, "Failed to get the number of CPU cores, set to default: ", cores);
            }

            ThreadPool<SockTask> pool;
            if (pool.init(SPSOCK_THREADPOOL_QUEUE_LENGTH, cores, SPSOCK_THREADPOOL_BATCH_SIZE_PROCESS) == false)
            {
                close(epollfd);
                epollfd = -1;
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "Failed to initialize thread pool: There is not enough memory space");
                return 17;
            }

            HSLL_LOGINFO(LOG_LEVEL_CRUCIAL, "Event loop started");
            std::unordered_set<int> ignore;
            UtilTask<bool(SPSOCK_THREADPOOL_BATCH_SIZE_SUBMIT - 1)>::type UtilTask(&pool, policy);

            while (exitFlag.load(std::memory_order_acquire))
            {
                int nfds = epoll_wait(epollfd, events, SPSOCK_MAX_EVENT_BSIZE, SPSOCK_EPOLL_TIMEOUT_MILLISECONDS);
                if (nfds == -1)
                {
                    if (errno == EINTR)
                        continue;

                    HSLL_LOGINFO(LOG_LEVEL_ERROR, "epoll_wait() failed: ", strerror(errno));
                    status |= 0x8;
                    return 11;
                }

                {
                    std::lock_guard<std::mutex> lock(list.mtx);
                    while (!list.connections.empty())
                    {
                        int fd = list.connections.front();
                        list.connections.pop_front();
                        if (connections.find(fd) != connections.end())
                            HSLL_LOGINFO(LOG_LEVEL_INFO, "Connection closed: ", CloseConnection(fd));

                        ignore.insert(fd);
                    }
                }

                for (int i = 0; i < nfds; i++)
                {
                    int fd = events[i].data.fd;

                    if (ignore.find(fd) != ignore.end())
                        continue;

                    if (fd == listenfd)
                    {
                        DealConnect();
                    }
                    else if (events[i].events & (EPOLLHUP | EPOLLERR))
                    {
                        HSLL_LOGINFO(LOG_LEVEL_INFO, "Connection closed: ", CloseConnection(fd));
                    }
                    else if (events[i].events & EPOLLIN)
                    {
                        auto it = connections.find(fd);
                        if (proc.rdp)
                        {
                            UtilTask.append(&it->second, proc.rdp);
                        }
                        else if (proc.wtp)
                        {
                            if (it->second.enableEvents(false, true))
                                CloseConnection(fd);
                        }
                        else
                        {
                            HSLL_LOGINFO(LOG_LEVEL_INFO, "Connection closed: ", CloseConnection(fd));
                        }
                    }
                    else if (events[i].events & EPOLLOUT)
                    {
                        auto it = connections.find(fd);
                        if (proc.wtp)
                        {
                            UtilTask.append(&it->second, proc.wtp);
                        }
                        else if (proc.rdp)
                        {
                            if (it->second.enableEvents(true, false))
                                CloseConnection(fd);
                        }
                        else
                        {
                            HSLL_LOGINFO(LOG_LEVEL_INFO, "Connection closed: ", CloseConnection(fd));
                        }
                    }
                }

                UtilTask.commit();
                ignore.clear();
            }

            status |= 0x8;
            HSLL_LOGINFO(LOG_LEVEL_CRUCIAL, "Event loop exited");
            return 0;
        }

        /**
         * @brief Configures linger options
         * @param enable Enable/disable lingering
         * @param waitSeconds Linger timeout (ignored if disabled)
         * @return 0 on success, error code on failure
         */
        int EnableLinger(bool enable, int waitSeconds = 5)
        {
            if (!enable)
            {
                lin.l_onoff = 0;
                return 0;
            }

            if (waitSeconds <= 0)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "Invalid parameter: waitSeconds must be positive");
                return 1;
            }

            lin = {1, waitSeconds};
            HSLL_LOGINFO(LOG_LEVEL_INFO, "Linger ", enable ? "enabled" : "disabled");
            return 0;
        }

        /**
         * @brief Configures TCP keep-alive options
         * @param enable Enable/disable keep-alive
         * @param aliveSeconds Idle time before probes
         * @param detectTimes Number of probes
         * @param detectInterval Interval between probes
         * @return 0 on success, error code on failure
         */
        int EnableKeepAlive(bool enable, int aliveSeconds = 120, int detectTimes = 3, int detectInterval = 10)
        {
            if (!enable)
            {
                alive.keepAlive = 0;
                return 0;
            }

            if (aliveSeconds <= 0 || detectTimes <= 0 || detectInterval <= 0)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "Invalid parameter: aliveSeconds, detectTimes, and detectInterval must be positive");
                return 1;
            }

            alive = {1, aliveSeconds, detectTimes, detectInterval};
            HSLL_LOGINFO(LOG_LEVEL_INFO, "Keep-alive ", enable ? "enabled" : "disabled");
            return 0;
        }

        /**
         * @brief Sets user-defined callbacks
         * @param cnp New connection callback
         * @param csp Close event callback
         * @param rdp Read event callback
         * @param wtp Write event callback
         * @return 0 on success, error code on failure
         */
        int SetCallback(ConnectProc cnp, CloseProc csp = nullptr, ReadProc rdp = nullptr, WriteProc wtp = nullptr)
        {
            if (cnp == nullptr)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "Invalid parameter: ConnectProc cannot be nullptr");
                return 1;
            }
            proc = {rdp, wtp, cnp, csp};
            status |= 0x2;
            HSLL_LOGINFO(LOG_LEVEL_INFO, "Callbacks configured successfully");
            return 0;
        }

        /**
         * @brief Configures exit signal handling
         * @param sg Signal number to handle
         * @param etp Event loop exit Callback
         * @param ctx Context of ExitProc
         * @return 0 on success, error code on failure
         * @note All connections are closed when exiting via a signal.
         * @note Therefore, you are allowed to call ExitProc before that to clean up the reference to the connection resource
         */
        int SetSignalExit(int sg, ExitProc etp = nullptr, void *ctx = nullptr)
        {
            struct sigaction sa;
            sa.sa_handler = DealExit;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;

            if (sigaction(sg, &sa, nullptr) == -1)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "sigaction() failed: ", strerror(errno));
                return 12;
            }

            this->exitProc = etp;
            this->exitCtx = ctx;

            status |= 0x4;
            HSLL_LOGINFO(LOG_LEVEL_INFO, "Exit signal handler configured for signal: ", sg);
            return 0;
        }

        /**
         * @brief Signals event loop to exit
         * @note Should be called after starting event loop
         */
        static void SetExitFlag()
        {
            exitFlag.store(false, std::memory_order_release);
        }

        /**
         * @brief Releases singleton resources
         */
        static void Release()
        {
            if (instance)
            {
                instance->Clean();
                delete instance;
                instance = nullptr;
            }
            HSLL_LOGINFO(LOG_LEVEL_INFO, "Instance released successfully");
        }

        /**
         * @brief Retrieves error description
         * @param code Error code index
         * @return Corresponding error string
         */
        const char *GetErrorStr(unsigned int code)
        {
            if (code < sizeof(SPSockErrors) / sizeof(SPSockErrors[0]))
                return SPSockErrors[code];

            return "Unknown error";
        }
    };

    /**
     * @brief Main UDP socket management class
     * @tparam address_family IP version (IPv4/IPv6)
     */
    template <ADDRESS_FAMILY address_family = ADDRESS_FAMILY::ADDRESS_FAMILY_INET>
    class SPSockUdp : noncopyable
    {
        void *ctx;    ///< User-defined context for callbacks
        RecvProc rcp; ///< Receive event callback

        int sockfd;          ///< Socket file descriptor
        unsigned int status; ///< Internal status flags

        static void *exitCtx;                       ///< Event loop exit ctx
        static ExitProc exitProc;                   ///< Event loop exit proc
        static std::atomic<bool> exitFlag;          ///< Event loop control flag
        static SPSockUdp<address_family> *instance; ///< Singleton instance

        /**
         * @brief Cleans up socket resources
         */
        void Clean()
        {
            if (sockfd != -1)
                close(sockfd);
        }

        /**
         * @brief Signal handler for graceful shutdown
         * @param sg Signal number received
         */
        static void DealExit(int sg)
        {
            if (SPSockUdp<address_family>::exitFlag)
            {
                HSLL_LOGINFO_NOPREFIX(LOG_LEVEL_CRUCIAL, "")
                HSLL_LOGINFO(LOG_LEVEL_CRUCIAL, "Caught signal ", sg, ", exiting event loop");
                SPSockUdp<address_family>::exitFlag.store(false, std::memory_order_release);
            }
        }

        /**
         * @brief Private constructor
         */
        SPSockUdp() : sockfd(-1), status(0) {};

    public:
        /**
         * @brief Gets singleton instance
         * @note Not thread-safe
         * @return Pointer to the singleton instance
         */
        static SPSockUdp *GetInstance()
        {
            if (instance == nullptr)
                instance = new SPSockUdp;

            return instance;
        }

        /**
         * @brief Binds socket to a specified port
         * @param port Port number to bind to
         * @return 0 on success, error code on failure
         */
        int Bind(unsigned short port) SPSOCK_ONE_TIME_CALL
        {
            if ((status & 0x1) == 0x1)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "Bind() cannot be called multiple times");
                return 15;
            }

            using SOCKADDR = SOCKADDR_IN<address_family>;
            typename SOCKADDR::TYPE addr;

            if ((sockfd = socket(address_family, PROTOCOL_UDP, 0)) == -1)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "socket() failed: ", strerror(errno));
                return 2;
            }

            int opt = 1;
            if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "setsockopt(SO_REUSEADDR) failed: ", strerror(errno));
                close(sockfd);
                sockfd = -1;
                return 3;
            }

            SOCKADDR::INIT(addr, port);
            if (bind(sockfd, (sockaddr *)&addr, sizeof(addr)) == -1)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "bind() failed: ", strerror(errno));
                close(sockfd);
                sockfd = -1;
                return 4;
            }

            status |= 0x1;
            HSLL_LOGINFO(LOG_LEVEL_INFO, "Bound to port: ", port, " successfully");
            return 0;
        }

        /**
         * @brief Main event processing loop for UDP
         * @param policy full load policy
         * @return 0 on success, error code on failure
         * @note One-time call function
         */
        int EventLoop(FULL_LOAD_POLICY policy = FULL_LOAD_POLICY_DISCARD) SPSOCK_ONE_TIME_CALL
        {
            if ((status & 0x8) == 0x8)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "EventLoop() cannot be called multiple times");
                return 15;
            }

            if ((status & 0x1) != 0x1)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "Bind() not called");
                return 16;
            }

            if ((status & 0x2) != 0x2)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "SetCallback() not called");
                return 14;
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
                    return 7;
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
            return 0;
        }

        /**
         * @brief Sends data to a specified IP and port
         * @param data Data buffer to send
         * @param size Size of data to send
         * @param ip Destination IP address
         * @param port Destination port number
         * @return 0 on success, error code on failure
         */
        int SendTo(const void *data, size_t size, const char *ip, unsigned short port)
        {
            if ((status & 0x1) != 0x1)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "Bind() not called");
                return 16;
            }

            if (data == nullptr || ip == nullptr || size == 0 || port == 0)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "Invalid parameter");
                return 1;
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
                    return 1;
                }
            }
            else
            {
                addr.sin6_family = AF_INET6;
                addr.sin6_port = htons(port);
                if (inet_pton(AF_INET6, ip, &addr.sin6_addr) <= 0)
                {
                    HSLL_LOGINFO(LOG_LEVEL_ERROR, "inet_pton() failed: invalid address");
                    return 1;
                }
            }

            if (sendto(sockfd, data, size, 0, (sockaddr *)&addr, sizeof(addr)) != size)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "sendto() failed: ", strerror(errno));
                return 8;
            }
            return 0;
        }

        /**
         * @brief Configures exit signal handling
         * @param sg Signal number to handle
         * @param etp Event loop exit Callback
         * @param ctx Context of ExitProc
         * @return 0 on success, error code on failure
         * @note All connections are closed when exiting via a signal.
         * @note Therefore, you are allowed to call ExitProc before that to clean up the reference to the connection resource
         */
        int SetSignalExit(int sg, ExitProc etp = nullptr, void *ctx = nullptr)
        {
            struct sigaction sa;
            sa.sa_handler = DealExit;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;

            if (sigaction(sg, &sa, nullptr) == -1)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "sigaction() failed: ", strerror(errno));
                return 12;
            }

            this->exitProc = etp;
            this->exitCtx = ctx;

            status |= 0x4;
            HSLL_LOGINFO(LOG_LEVEL_INFO, "Exit signal handler configured for signal: ", sg);
            return 0;
        }

        /**
         * @brief Sets receive callback
         * @param rcp Receive event callback
         * @param ctx User-defined context (optional)
         * @return 0 on success, error code on failure
         */
        int SetCallback(RecvProc rcp, void *ctx = nullptr)
        {
            if (rcp == nullptr)
            {
                HSLL_LOGINFO(LOG_LEVEL_ERROR, "Invalid parameter: RecvProc cannot be nullptr");
                return 1;
            }

            this->rcp = rcp;
            this->ctx = ctx;
            status |= 0x2;
            HSLL_LOGINFO(LOG_LEVEL_INFO, "Callback configured successfully");
            return 0;
        }

        /**
         * @brief Signals event loop to exit
         * @note Should be called after starting event loop
         */
        static void SetExitFlag()
        {
            exitFlag.store(false, std::memory_order_release);
        }

        /**
         * @brief Releases singleton resources
         */
        static void Release()
        {
            if (instance)
            {
                instance->Clean();
                delete instance;
                instance = nullptr;
            }

            HSLL_LOGINFO(LOG_LEVEL_INFO, "Instance released successfully");
        }

        /**
         * @brief Retrieves error description
         * @param code Error code index
         * @return Corresponding error string
         */
        const char *GetErrorStr(unsigned int code)
        {
            if (code < sizeof(SPSockErrors) / sizeof(SPSockErrors[0]))
                return SPSockErrors[code];

            return "Unknown error";
        }
    };

    // Static member initialization
    unsigned int SOCKADDR_IN<ADDRESS_FAMILY_INET>::UDP_MAX_BSIZE = 65535;
    unsigned int SOCKADDR_IN<ADDRESS_FAMILY_INET6>::UDP_MAX_BSIZE = 65527;

    template <ADDRESS_FAMILY address_family>
    std::atomic<bool> SPSockTcp<address_family>::exitFlag = true;

    template <ADDRESS_FAMILY address_family>
    std::atomic<bool> SPSockUdp<address_family>::exitFlag = true;

    template <ADDRESS_FAMILY address_family>
    void *SPSockTcp<address_family>::exitCtx = nullptr;

    template <ADDRESS_FAMILY address_family>
    void *SPSockUdp<address_family>::exitCtx = nullptr;

    template <ADDRESS_FAMILY address_family>
    ExitProc SPSockTcp<address_family>::exitProc = nullptr;

    template <ADDRESS_FAMILY address_family>
    ExitProc SPSockUdp<address_family>::exitProc = nullptr;

    template <ADDRESS_FAMILY address_family>
    SPSockTcp<address_family> *SPSockTcp<address_family>::instance = nullptr;

    template <ADDRESS_FAMILY address_family>
    SPSockUdp<address_family> *SPSockUdp<address_family>::instance = nullptr;
}
#endif