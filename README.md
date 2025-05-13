# SPSock 网络库

## 特性概览

- 🚀 高性能TCP/UDP网络通信框架
- ⚡ 基于epoll的事件驱动模型
- 🛡️ 线程安全的连接管理
- 🔧 支持IPv4/IPv6双协议栈
- 🔌 可配置的Keep-Alive/Linger机制
- 📝 多粒度日志控制系统
- 🔄 双缓冲队列设计（读/写分离）

## 目录

1. [构建](#构建)
2. [快速开始](#快速开始)
3. [SPSock关键函数](#spsock关键函数说明)
4. [SPController关键函数](#spcontroller关键函数说明)
5. [SPConfig配置说明](#spconfig配置说明)
6. [注意事项](#注意事项)

## 构建

### makefile构建

```bash
make [debug=1] [static=1] [test=1]
```

| 参数       | 说明                      | 示例                 |
|------------|---------------------------|----------------------|
| debug=1    | 启用调试模式              | `make debug=1`       |
| static=1   | 生成静态库                | `make static=1`      |
| test=1     | 编译测试样例              | `make test=1`        |

### cmake构建

```bash
mkdir -p build && cd build
cmake .. [选项]
make
```

| 选项                      | 说明                     |
|---------------------------|--------------------------|
| -DCMAKE_BUILD_TYPE=Debug   | 启用调试模式           |
| -DBUILD_STATIC=ON          | 生成静态库             |
| -DBUILD_TEST=ON            | 编译测试样例           |

## 快速开始

### TCP服务端示例
```cpp
#include "SPSock.hpp"
void echo_read_write_proc(SOCKController *controller)
{
    if (controller->isPeerClosed())//对端关闭
    {
        controller->close();
        return;
    }

    if (!controller->writeBack())//回传
    {
        controller->close();
    }
    else
    {
        bool ret;

        if (controller->getReadBufferSize())//未发送完毕
            ret = controller->enableEvents(false, true);
        else
            ret = controller->enableEvents(true, false);

        if (!ret)//错误处理
            controller->close();
    }
}

int main()
{
    SPSockTcp<ADDRESS_FAMILY_INET>::Config();//配置

    auto ins = SPSockTcp<ADDRESS_FAMILY_INET>::GetInstance();//获取实例

    if (ins->EnableKeepAlive(true, 120, 2, 10) == false)//设置keepalive
        return -1;
    if (ins->SetCallback(nullptr, nullptr, echo_read_write_proc, echo_read_write_proc) == false)//设置读写回调
        return -1;
    if (ins->SetSignalExit(SIGINT) == false)//设置退出信号
        return -1;
    if (ins->Listen(4567) == false)//设置监听端口
        return -1;

    ins->EventLoop();//事件循环
    ins->Release();//释放实例
    return 0;
}
```

### UDP服务端示例

```cpp
void echo_rcp(void *ctx, const char *data, ssize_t size, const char *ip, unsigned short port)
{
    auto ins = (SPSockUdp<ADDRESS_FAMILY_INET> *)ctx;
    ins->SendTo(data, size, ip, port);//数据回传
}

int main()
{
    SPSockUdp<ADDRESS_FAMILY_INET>::Config();//配置
    
    auto ins = SPSockUdp<ADDRESS_FAMILY_INET>::GetInstance();//获取实例

    if (ins->Bind(4567)==false)//绑定端口
        return -1;

    if (ins->SetCallback(echo_rcp, ins)==false)//设置回调
        return -1;

    if (ins->SetSignalExit(SIGINT)==false)//设置退出信号
        return -1;

    ins->EventLoop();//事件循环
    ins->Release();//释放实例
}
```

## SPSock关键函数说明

| 函数名              | 说明                                                                 | 重要参数                             |
|---------------------|----------------------------------------------------------------------|--------------------------------------|
| Listen()            | 启动指定端口的监听                                                   | port: 监听端口                       |
| EventLoop()         | 启动事件循环处理网络事件                                             | policy: 线程池满载策略               |
| SetCallback()       | 设置各类事件回调函数                                                 | 支持连接/关闭/读/写回调              |
| EnableKeepAlive()   | 配置TCP保活机制                                                     | enable: 开关, aliveSeconds: 空闲时间 |
| SetSignalExit()     | 设置信号处理函数实现优雅退出                                         | sg: 捕获的信号                       |
| SetWaterMark()      | 设置读写缓冲区水位线                                                 | readMark/writeMark: 触发阈值         |

## SPController关键函数说明

| 函数名            | 说明                                                                 |
|-------------------|----------------------------------------------------------------------|
| read()            | 从读缓冲区取出数据                                                   |
| write()           | 直接写入套接字（非缓冲）                                             |
| writeTemp()       | 写入写缓冲区（延迟发送）                                             |
| commitWrite()     | 提交缓冲区数据到套接字                                               |
| getReadBufferSize() | 获取可读数据量                                                       |
| enableEvents()    | 重新启用指定事件监听                                                 |

## SPConfig配置说明

| 参数名                          | 说明                                                                 | 限制条件                                                                 |
|---------------------------------|----------------------------------------------------------------------|--------------------------------------------------------------------------|
| READ_BSIZE                     | 读缓冲区大小                                                        | 必须为1024的倍数，≥1KB                                                  |
| WRITE_BSIZE                    | 写缓冲区大小                                                        | 必须为1024的倍数，≥1KB                                                  |
| BUFFER_POOL_PEER_ALLOC_NUM     | 缓冲池单次分配块数                                                  | 1-1024                                                                  |
| BUFFER_POOL_MIN_BLOCK_NUM      | 缓冲池最小块数                                                      | ≥ BUFFER_POOL_PEER_ALLOC_NUM                                            |
| EPOLL_MAX_EVENT_BSIZE          | 单次epoll循环处理的最大事件数                                        | 1-65535                                                                 |
| EPOLL_TIMEOUT_MILLISECONDS     | epoll等待超时时间（毫秒）                                           | -1: 永久阻塞，0: 非阻塞立即返回，>0: 指定超时时间                       |
| EPOLL_DEFAULT_EVENT            | 默认epoll事件监听类型                                               | 有效组合：EPOLLIN、EPOLLOUT 或 EPOLLIN\|EPOLLOUT                        |
| THREADPOOL_QUEUE_LENGTH        | 线程池任务队列最大容量                                              | 1-1048576                                                              |
| THREADPOOL_BATCH_SIZE_SUBMIT   | 批量提交任务到线程池的批处理大小                                    | < THREADPOOL_QUEUE_LENGTH                                              |
| THREADPOOL_BATCH_SIZE_PROCESS  | 线程池处理任务的批处理大小                                          | 1-1024                                                                 |
| WORKER_THREAD_RATIO            | 工作线程与IO线程的权重比                                            | 0.0 < ratio < 1.0                                                      |
| MIN_LOG_LEVEL                  | 最低日志输出等级                                                    | 有效枚举值：LOG_LEVEL_INFO, LOG_LEVEL_WARNING, LOG_LEVEL_CRUCIAL, LOG_LEVEL_ERROR |

## 注意事项

1.必须在获取实例前调用`Config()`初始化配置

2.实例获取后必须通过`Release()`释放

3.读写回调在线程池内进行,连接建立和关闭回调在线程循环中进行

4.每次触发回调后必须调用`enableEvents()`重新启用指定事件监听

5.对端关闭且读取完所有数据后应当立即调用`SOCKController`的`close`方法释放资源
