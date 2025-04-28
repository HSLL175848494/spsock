# SPSock 网络库

## 特性概览

- 🚀 高性能TCP/UDP网络通信框架
- ⚡ 基于epoll的事件驱动模型
- 🛡️ 线程安全的连接管理
- 🔧 支持IPv4/IPv6双协议栈
- 🔌 可配置的Keep-Alive/Linger机制
- 📝 多粒度日志控制系统
- 🔄 双缓冲队列设计（读/写分离）
- 📡 零拷贝数据回传支持

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

## 核心API说明（SOCKController）

### 连接状态管理
| 方法               | 说明                                                                 |
|--------------------|----------------------------------------------------------------------|
| **isPeerClosed()** | 检测远端是否关闭连接，返回`true`时应触发资源释放                     |
| **close()**        | 主动关闭连接，应在错误或通信结束时调用                               |

### 直接数据读写
| 方法                           | 说明                                                                 |
|--------------------------------|----------------------------------------------------------------------|
| **read(void *buf, size_t len)** | 从读缓冲区读取数据，返回实际读取字节数（非阻塞）                     |
| **write(const void *buf, size_t len)** | 直接发送数据，返回成功字节数，`0`需等待可写事件，`-1`需关闭连接      |

### 缓冲队列操作
| 方法                          | 说明                                                                 |
|-------------------------------|----------------------------------------------------------------------|
| **writeTemp()**               | 数据暂存至写缓冲区，返回实际写入字节数                              |
| **commitWrite()**             | 提交写缓冲数据，返回剩余未发送字节数                                |
| **getReadBufferSize()**       | 获取读缓冲区数据量（字节）                                          |
| **getWriteBufferSize()**      | 获取写缓冲区待发送数据量（字节）                                    |

### 数据回传控制
| 方法                     | 说明                                                                 |
|--------------------------|----------------------------------------------------------------------|
| **writeBack()**          | 直写读缓冲数据到socket，返回`false`表示需关闭连接                   |
| **moveToWriteBuffer()**  | 将读缓冲数据移至写缓冲（零拷贝），返回移动字节数                    |

### 事件管理
| 方法                       | 说明                                                                 |
|----------------------------|----------------------------------------------------------------------|
| **enableEvents()**         | 动态启用读/写事件监听，失败需关闭连接                               |
| **renableEvents()**        | 恢复最近事件配置                                                   |

### 上下文访问
| 方法             | 说明                                |
|------------------|-------------------------------------|
| **getCtx()**     | 获取用户绑定的上下文指针            |

## 最佳实践

### 数据转发模式
```cpp
// 零拷贝回传模式（推荐）
if (!ctrl->writeBack()) {
    ctrl->close();
}

// 缓冲中转模式
size_t moved = ctrl->moveToWriteBuffer();
if (moved > 0) {
    ssize_t remain = ctrl->commitWrite();
    if (remain == -1) {
        ctrl->close();
    }
}
```

### 错误处理流程
```cpp
ssize_t sent = ctrl->write(data, len);
if (sent == -1) {
    // 严重错误立即关闭
    ctrl->close();
} else if (sent == 0) {
    // 临时不可写，启用写事件监听
    if (!ctrl->enableEvents(false, true)) {
        ctrl->close();
    }
}
```

## 性能调优

1. **缓冲区配置**  
   通过`SPConfig`调整读写缓冲区大小：
   ```cpp
   SPConfig cfg {
       .READ_BSIZE = 1024 * 1024,  // 1MB读缓冲
       .WRITE_BSIZE = 512 * 1024   // 512KB写缓冲
   };
   SPSock::Config(cfg);
   ```

2. **批量提交优化**  
   使用`writeTemp`+`commitWrite`组合减少系统调用：
   ```cpp
   // 批量写入缓冲区
   size_t total = ctrl->writeTemp(batchData, batchSize);
   // 统一提交
   ssize_t remain = ctrl->commitWrite();
   ```

3. **事件控制**  
   动态调整事件订阅提升吞吐量：
   ```cpp
   // 数据积压时暂停读事件
   if (ctrl->getWriteBufferSize() > HIGH_WATERMARK) {
       ctrl->enableEvents(true, false);
   }
   ```

## 监测指标

| 指标名称                | 获取方法                     | 说明                      |
|-------------------------|------------------------------|---------------------------|
| 活跃连接数              | `GetConnectionCount()`       | 当前维护的连接总数        |
| 读缓冲使用率            | `getReadBufferSize()`        | 当前连接读缓冲数据量      |
| 写缓冲积压量            | `getWriteBufferSize()`       | 待发送数据字节数          |
| 事件循环吞吐量          | 自定义统计逻辑               | 单位时间内处理事件数      |