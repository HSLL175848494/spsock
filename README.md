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

###示例

main.cpp:
```
#include"SPSock.h"
```
编译链接:
```
g++ -o2 main.cpp -o test -lSPsock
```
##
