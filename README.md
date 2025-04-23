# SPSock 网络库

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

## 库引入

包含include目录下的SPSock.h并链接libSPsock.so/libSPsock.a

### 示例

main.cpp:
```
#include"SPSock.h"
```
编译链接:
```
g++ -o2 main.cpp -o test -lSPsock
```
##快速开始

### TCP服务器

```
int main()
{
    SPSockTcp<ADDRESS_FAMILY_INET>::Config();//填充默认配置

    auto ins = SPSockTcp<ADDRESS_FAMILY_INET>::GetInstance();//获取实例

    if (ins->EnableKeepAlive(true, 120, 2, 10) == false)//设置保持活跃链接参数
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
