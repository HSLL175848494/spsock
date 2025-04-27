# SPSock 网络库

## 特性概览

- 🚀 高性能TCP/UDP网络通信框架
- ⚡ 基于epoll的事件驱动模型
- 🛡️ 线程安全的连接管理
- 🔧 支持IPv4/IPv6双协议栈
- 🔌 可配置的Keep-Alive/Linger机制
- 📝 多粒度日志控制系统

## 构建

### makefile构建


| 参数       | 说明                      | 示例                 |
|------------|---------------------------|----------------------|
| debug=1    | 启用调试模式              | `make debug=1`       |
| static=1   | 生成静态库                | `make static=1`      |
| test=1     | 编译测试样例              | `make test=1`        |

### cmake构建

```
mkdir -p build && cd build
cmake ..
make
```

| 参数       | 说明                      | 示例                 |
|------------|---------------------------|----------------------|
| -DCMAKE_BUILD_TYPE=Debug   | 启用调试模式       | `cmake .. -DCMAKE_BUILD_TYPE=Debug`       |
| -DBUILD_STATIC=ON   | 生成静态库                | `cmake .. -DBUILD_STATIC=ON`      |
| -DBUILD_TEST=ON     | 编译测试样例              | `cmake .. -DBUILD_TEST=ON`        |


## 快速开始

### TCP服务器

```cpp
int main()
{
    SPSock::Config();//填充默认配置

    auto ins = SPSockTcp<ADDRESS_FAMILY_INET>::GetInstance();//获取实例

    if (ins->EnableKeepAlive(true, 120, 2, 10) == false)//设置KeepAlive
        return -1;
    if (ins->EnableLinger(true, 5)== false)//设置优雅关闭连接
        return -1;
    if (ins->SetCallback(nullptr, nullptr, echo_read_write_proc, echo_read_write_proc) == false)//设置连接、关闭连接以及读写回调
        return -1;
    if (ins->SetSignalExit(SIGINT) == false)//设置退出信号
        return -1;
    if (ins->Listen(4567) == false)//开始监听
        return -1;

    ins->EventLoop();//事件循环

    ins->Release(); //释放实例
    return 0;
}
```
### UDP服务器

```cpp
int main()
{
    SPSock::Config();//填充默认配置

    auto ins = SPSockUdp<ADDRESS_FAMILY_INET>::GetInstance();//获取实例

    if (ins->Bind(4567)==false)//绑定端口
        return -1;

    if (ins->SetCallback(echo_rcp, ins)==false)//设置recv回调
        return -1;

    if (ins->SetSignalExit(SIGINT)==false)//设置退出信号
        return -1;

    ins->EventLoop();//事件循环

    ins->Release(); //释放实例
}
```
## 核心API说明(连接控制器SOCKController)

### 连接状态管理
- **isPeerClosed()**  
  检测远端是否关闭连接，返回`true`时应触发资源释放。

- **close()**  
  主动关闭连接，应在错误或通信结束时调用。

---

### 直接数据读写
- **read(void *buf, size_t len)**  
  从读缓冲区读取数据，返回实际读取字节数。非阻塞，若缓冲区空则返回0。

- **write(const void *buf, size_t len)**  
  直接向Socket发送数据。  
  - 返回：成功发送的字节数；`0`表示需等待可写事件（EAGAIN）；`-1`表示错误（需调用`close()`或启用事件重试）。

---

### 缓冲队列操作
- **writeTemp(const void *buf, size_t len)**  
  将数据暂存至写缓冲区，返回实际写入缓冲区的字节数（受缓冲区剩余空间限制）。

- **commitWrite()**  
  将写缓冲区的数据提交发送至Socket。  
  - 返回：剩余未发送字节数；`-1`表示错误（需关闭或重设事件）。

- **getReadBufferSize() / getWriteBufferSize()**  
  获取读/写缓冲区的当前数据量（字节数）。

---

### 数据回传控制
- **writeBack()**  
  将读缓冲区的数据直接回写至Socket。优先发送写缓冲区内容，再尝试直写读缓冲数据。  
  - 返回：`false`表示Socket错误需关闭连接。

- **moveToWriteBuffer()**  
  将读缓冲区的数据移至写缓冲区（不触发I/O），返回移动的字节数。适用于数据加工后转发场景。

---

### 事件管理
- **enableEvents(bool read, bool write)**  
  动态启用/禁用Socket的读/写事件监听。失败需关闭连接。

- **renableEvents()**  
  恢复Socket的事件监听为最近一次配置。

---

### 上下文访问
- **getCtx()**  
  获取用户绑定的上下文指针，可用于传递会话状态或业务数据。

---

### 关键特性
- **线程安全**：所有I/O操作内置同步机制，支持多线程调用。
- **双缓冲设计**：读/写分离缓冲减少锁竞争，`writeTemp`+`commitWrite`支持批量提交优化。
- **高效反射**：`writeBack`和`moveToWriteBuffer`避免内存拷贝，提升转发性能。

## 全局配置

通过调用 `SPSock::Config(SPConfig)`指定

 `SPConfig` 结构体成员

| 成员名称                      | 类型        | 说明                                                                 |
|-------------------------------|-------------|----------------------------------------------------------------------|
| `READ_BSIZE`                  | `int`       | 读缓冲区大小                                                         |
| `WRITE_BSIZE`                 | `int`       | 写缓冲区大小                                                         |
| `BUFFER_POOL_PEER_ALLOC_NUM`  | `int`       | 缓冲内存池单次申请块数量                                      |
| `BUFFER_POOL_MIN_BLOCK_NUM`   | `int`       | 缓冲内存池块最小数量                                            |
| `EPOLL_MAX_EVENT_BSIZE`       | `int`       | epoll 单次最大事件接收数量                                            |
| `EPOLL_TIMEOUT_MILLISECONDS`  | `int`       | epoll 等待超时时间（毫秒），-1 表示无限等待                          |
| `EPOLL_DEFAULT_EVENT`         | `int`       | epoll 默认监听的事件类型（如 `EPOLLIN`、`EPOLLOUT` 或 `EPOLLIN\|EPOLLOUT`） |
| `THREADPOOL_QUEUE_LENGTH`     | `int`       | 线程池任务队列的最大长度                                             |
| `THREADPOOL_DEFAULT_THREADS_NUM` | `int`    | 当无法确定系统核心数时，线程池的默认线程数                           |
| `THREADPOOL_BATCH_SIZE_SUBMIT` | `int`     | 单次提交到线程池的任务批处理大小                                     |
| `THREADPOOL_BATCH_SIZE_PROCESS` | `int`    | 线程池单次处理的任务批处理大小                                       |
| `MIN_LOG_LEVEL`               | `LOG_LEVEL` | 最低日志打印级别                                                     |

## 日志示例

```
[CRUCIAL] Event loop started
[CRUCIAL] Caught signal 2, exiting event loop
[CRUCIAL] Event loop exited
```

## 注意事项

1. **必须首先调用** `SPSock::Config()` 进行全局配置
2. GetInstance() 非线程安全，建议在主线程初始化
3. EventLoop() 为阻塞调用，通常需要放在独立线程
4. 写操作失败时应调用 Close() 或 EnableEvent()
5. 释放资源请调用对应类的 Release() 方法
6. 读写回调将在线程池内被调用,连接建立和关闭回调在事件循环线程被调用
