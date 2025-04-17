# SPSock 网络库

## [v1.1] - 2025-04-17

### 新增
- 读写任务线程池
- server满载策略: 等待/丢弃任务
## 概述

SPSock 提供了TCP/UDP 套接字封装，支持 IPv4/IPv6 双协议栈。主要特性包括：

- 事件驱动模型（基于 epoll）
- 异步非阻塞 I/O 操作
- 线程安全连接管理
- TCP Keep-Alive/Linger 配置
- 信号驱动的优雅退出机制
- 单例模式实现
- 可扩展回调函数体系

## 快速开始

见sample

## 功能

### 配置选项

#### TCP 特殊配置
```cpp
// 启用 Keep-Alive（空闲120秒，探测3次，间隔10秒）
tcp->EnableKeepAlive(true, 120, 3, 10);

// 启用 Linger（关闭时等待5秒）
tcp->EnableLinger(true, 5);
```

#### 信号处理
```cpp
// 注册退出信号（支持多个信号）
tcp->SetSignalExit(SIGTERM);  // 终止信号
tcp->SetSignalExit(SIGQUIT);  // 退出信号
```

### 连接管理

通过 `SOCKController` 进行 I/O 操作：
```cpp
// 非阻塞写
ssize_t sent = controller.Write(data, len);
if(sent == -1) {
    controller.Close();  // 错误时关闭连接
}

// 重新启用写事件
controller.EnableEvent(true, true);
```

## 错误处理

获取错误描述：
```cpp
int error_code = tcp->Listen(port);
if(error_code != 0) {
    std::cerr << tcp->GetLastError(error_code);
}
```
## 日志

正常情况下控制台会打印log级别大于等于LOG_LEVEL_WARNING的信息(可通过修改`HSLL_MIN_LOG_LEVEL`定义更改)

如果您预定义了_DEBUG或DEBUG_宏，控制台则会打印所有日志信息


<img src="https://github.com/user-attachments/assets/fd1c3ec0-e780-4b67-8339-1c502629901f" width="900px">


如果您预定义了_NOLOG或NOLOG_宏，则所有日志信息均不打印

## 注意事项

1. **单例模式**：通过 `GetInstance()` 获取实例，使用后必须调用 `Release()`
2. **线程安全**：`EventLoop()` 应在主线程运行，I/O 操作支持多线程
3. **事件接收**：每次读写回调触发必须调用 `SOCKController`的`EnableEvent()`方法以接收下一次事件
4. **事件处理**：请注意连接/关闭事件是在事件循环中进行，而读写事件是在线程池中执行
5. **性能调优**：适当调整宏定义：
   ```cpp
   #define SPSOCK_MAX_EVENT_BSIZE 10000  // 最大处理事件数
   #define SPSOCK_EPOLL_TIMEOUT_MILLISECONDS 500  // epoll 超时
   #define SPSOCK_THREADPOOL_QUEUE_LENGTH 10000    ///< 任务队列长度
   #define SPSOCK_THREADPOOL_BATCH_SIZE_SUBMIT 10  ///< 任务批提交大小
   #define SPSOCK_THREADPOOL_BATCH_SIZE_PROCESS 5  ///< 任务批处理数目
   ```

## 依赖项

- C++17 或更高版本
- Linux 系统（依赖 epoll）
