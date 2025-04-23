# SPSock 网络库（当前文件内容过期）

## 特性概览

- 🚀 高性能TCP/UDP网络通信框架
- ⚡ 基于epoll的事件驱动模型
- 🛡️ 线程安全的连接管理
- 🔧 支持IPv4/IPv6双协议栈
- 🔌 可配置的Keep-Alive/Linger机制
- 📊 内置智能负载均衡策略
- 📝 多粒度日志控制系统

## 编译选项

使用附带的Makefile支持以下编译参数：

| 参数       | 说明                      | 示例                 |
|------------|---------------------------|----------------------|
| debug=1    | 启用调试模式              | `make debug=1`       |
| static=1   | 生成静态库                | `make static=1`      |
| test=1     | 编译测试样例              | `make test=1`        |

## 注意事项

1. **必须首先调用** `SPSock::Config()` 进行全局配置
2. GetInstance() 非线程安全，建议在主线程初始化
3. EventLoop() 为阻塞调用，通常需要放在独立线程
4. 写操作失败时应调用 Close() 或 EnableEvent()
5. 释放资源请调用对应类的 Release() 方法

## 快速开始

见sample

## 配置
```
struct SPConfig
{
    ///< 读缓冲区大小
    int READ_BSIZE;
    ///< 写缓冲区大小
    int WRITE_BSIZE;
    ///< 每个epoll周期处理的最大事件数
    int MAX_EVENT_BSIZE;
    ///< epoll等待超时时间（毫秒，-1表示无限等待）
    int EPOLL_TIMEOUT_MILLISECONDS;
    ///< epoll默认监听的事件类型（EPOLLIN EPOLLOUT EPOLLIN|EPOLLOUT）
    int EPOLL_DEFAULT_EVENT;
    ///< 线程池任务队列最大长度
    int THREADPOOL_QUEUE_LENGTH;
    ///< 当无法获取系统核心数时使用的默认线程数
    int THREADPOOL_DEFAULT_THREADS_NUM;
    ///< 单次批量提交给线程池的任务数
    int THREADPOOL_BATCH_SIZE_SUBMIT;
    ///< 线程池单次批量处理的任务数
    int THREADPOOL_BATCH_SIZE_PROCESS;
    ///< 最低日志打印级别
    LOG_LEVEL MIN_LOG_LEVEL;
};
```


### 日志示例


<img src="https://github.com/user-attachments/assets/fd1c3ec0-e780-4b67-8339-1c502629901f" width="900px">
