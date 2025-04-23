# SPSock 网络库

## 特性概览

- 🚀 高性能TCP/UDP网络通信框架
- ⚡ 基于epoll的事件驱动模型
- 🛡️ 线程安全的连接管理
- 🔧 支持IPv4/IPv6双协议栈
- 🔌 可配置的Keep-Alive/Linger机制
- 📊 内置智能负载均衡策略
- 📝 多粒度日志控制系统

## 快速开始

见sample

## 核心配置

### 编译选项（SPTypes.h）
```cpp
#define SPSOCK_READ_BSIZE      16384   // 16KB读缓冲区
#define SPSOCK_WRITE_BSIZE     32768   // 32KB写缓冲区
#define SPSOCK_MAX_EVENT_BSIZE 5000    // 单次epoll最大处理事件数
#define SPSOCK_EPOLL_TIMEOUT   -1      // epoll无限等待
#define SPSOCK_THREADPOOL_QUEUE_LENGTH 10000 // 线程池队列最大任务数量
```

### 运行时配置
```cpp
// TCP Keep-Alive配置（单位：秒）
EnableKeepAlive(true, 
    120,    // 空闲超时
    3,      // 探测次数
    10      // 探测间隔
);

// Linger配置
EnableLinger(true, 5);  // 启用并等待5秒

// 满载策略选择
EventLoop(HSLL::FULL_LOAD_POLICY_WAIT);    // 队列满时等待
EventLoop(HSLL::FULL_LOAD_POLICY_DISCARD); // 丢弃新任务
```

## 核心API

### 连接控制器（SOCKController）

```cpp
// 非阻塞读取（返回实际读取字节数）
size_t read(void* buf, size_t len);

// 直接发送（返回实际发送字节数）
ssize_t write(const void* buf, size_t len);

// 缓冲发送（需配合commitWrite使用）
size_t writeTemp(const void* buf, size_t len);
ssize_t commitWrite();

// 事件控制（成功返回true）
bool enableEvents(bool read, bool write);
```

### 高级特性

#### 1. 双缓冲策略
- **读缓冲**：自动管理接收缓冲区，防止数据分包
- **写缓冲**：支持直接发送和缓冲发送两种模式

#### 2. 智能重试机制
```cpp
// 内核级的EINTR/EAGAIN自动重试
ssize_t ret = send(fd, buf, len, MSG_NOSIGNAL);
if(ret == -1)
{
    // 自动处理信号中断和临时不可用情况
}
```

## 错误处理

### 错误处理示例
```cpp
int ret = tcp->Listen(8080);
if(ret != 0)
{
    std::cerr << "Error (" << ret << "): "  << tcp->GetErrorStr(ret) << std::endl;
    exit(EXIT_FAILURE);
}
```

## 性能调优

### 推荐配置
```cpp
// 高吞吐场景
#define SPSOCK_THREADPOOL_QUEUE_LENGTH 20000
#define SPSOCK_MAX_EVENT_BSIZE 10000
#define SPSOCK_THREADPOOL_BATCH_SIZE_PROCESS 10

// 低延迟场景
#define SPSOCK_EPOLL_TIMEOUT_MILLISECONDS 10
#define SPSOCK_THREADPOOL_BATCH_SIZE_SUBMIT 5
```

## 日志系统

### 日志级别控制
```cpp
#define HSLL_MIN_LOG_LEVEL LOG_LEVEL_WARNING  // 默认显示警告及以上
// 可用级别：DEBUG, INFO, WARNING, ERROR, CRUCIAL
```

### 特殊编译选项
```bash
# 调试模式（显示所有日志）
g++ -D_DEBUG ...

# 生产模式（禁用日志）
g++ -D_NOLOG ...
```

### 日志示例


<img src="https://github.com/user-attachments/assets/fd1c3ec0-e780-4b67-8339-1c502629901f" width="900px">



## 最佳实践

1. **连接管理**
```cpp
// 正确的事件启用顺序
ctrl->writeTemp(data, len);    // 填充缓冲区
ctrl->commitWrite();           // 尝试立即发送
if(ctrl->enableEvents(false, true))
{
    // 注册写事件成功
} else
{
    ctrl->close();  // 注册失败立即关闭
}
```

2. **资源释放**
```cpp
// 必须成对调用
auto instance = SPSockTcp<>::GetInstance();
// ... 使用实例 ...
instance->Release();  // 确保释放系统资源
```

3. **信号处理**
```cpp
// 注册多个退出信号
tcp->SetSignalExit(SIGTERM);
tcp->SetSignalExit(SIGQUIT);
tcp->SetSignalExit(SIGUSR1);
```
