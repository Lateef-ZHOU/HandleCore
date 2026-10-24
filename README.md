# HandleCore

独立的 C++20 / Qt Core 通信基类库。首版从 LiDAR_DataACQ 提交 `8f792a8d976089e3eba8edd7ae0fbedbf32e72f6` 的 `include/Device/Base/HandleBase.h` 原样迁入，构建与测试一并迁移。此处是基类的唯一维护源；具体传输子类目前仍留在应用仓库。

Handle 按通信方式和协议复用，不按 Windows 设备或 Linux 设备划分。Linux 桥接器和 Windows 官邸均可直接使用同一具体 Handle；串口、UDP、激光器、温控或相机传输只有实际操作系统 API 差异才需要适配。设备业务配置恢复由子类或上层负责，不进入基类。本次未迁移或重新设计子类，尚未验证 ARM 和实物设备。

## 依赖与使用

- CMake 3.28 或以上、C++20、Qt Core；不依赖 QML、Gui、Quick、Qt Test、Npcap、libserialport 或本项目 Device/Collector。
- `HandleCore` 是头文件目标，别名为 `Lidar::HandleCore`；使用方通过 FetchContent 固定本库的完整提交号，或 `add_subdirectory(<本库目录>)`，并链接该目标，即取得头文件路径、C++20 要求和 Qt Core 链接依赖。
- 独立调用方包含 `<HandleBase.h>`。基类的 `RxTime` 直接别名到 `std::chrono::system_clock::time_point`；应用自己的 `RxTime.h` 仍负责 Qt 元类型及 QML 登记，二者没有类型包装或转换。
- 需要 Qt 信号的具体类型使用 `Q_OBJECT` / `Q_SIGNALS` 并启用 AUTOMOC；模板基类只声明纯虚通知函数，不依赖 QML，也不替具体类登记信号。

## 独立配置和验证

在仓库根目录执行，Qt 已可被 CMake 找到时可省略安装前缀参数：

```text
cmake -S . -B build -DCMAKE_PREFIX_PATH="<Qt 安装前缀>"
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

Makefiles 等单配置生成器在配置时加 `-DCMAKE_BUILD_TYPE=Release`。Windows 测试会为子进程补齐所选 Qt Core DLL 的目录；不需要 GUI 平台插件。`BUILD_TESTING=OFF` 可仅配置接口目标，不构建测试。

独立入口只编译基类及 `tests/`。作为其他项目的子目录时不添加本库测试、不修改使用方的测试开关。主程序通过相同目标引用基类，仍保留自身原有的界面及传输依赖。

## 当前接口契约

- `HandleParameters` 要求参数可默认构造、可从 const 引用赋值，且 `current != incoming` 可用于条件。不要求复制构造、赋值返回引用或完整相等运算集合。
- 所属线程调用 `start(params)`、`send(payload)`、`stop()`，其他线程可观察 `isOpen()`。继承的 QThread 启停方法不替代 Handle 生命周期入口。
- 构造时提供可调用的数据回调，回调在收包线程同步执行。字节可能借用外部缓冲；异步留存者负责数据所有权，RxTime 可按值复制。
- 一次 `onRX` 产生一项裁决：Decode 上交一份数据，Reject 仅回发一份响应，Drop 无动作。`onTX` 的 Encode 交给 `connSend`，Drop 返回失败。接口及行为未扩展。
- 首次打开失败即退出；建立过连接后 I/O 失败按原有一秒间隔重连。连接通知只在首连成功与最终退出发送，中途重连透明。设备配置恢复归具体实现或上层业务。
- `stop()` 先记录停止请求、促断，再等待收包线程与同步回调退出。所有者和具体类在拆除回调依赖前执行既有同步关闭，不增加状态位、超时或第二条清理链。

## 验证覆盖

`handle_core` 使用 QCoreApplication 和受控传输，不访问实物：检查字节/时间交付、三种 RX 与两种 TX 裁决、发送失败、Qt 信号回所属线程、重复关闭、回调期间关闭、打开期间关闭、首次连接失败及后续重连。屏障用于确定执行次序，测试超时仅防止测试挂起，不改变生产等待行为。

四项负向编译测试分别拒绝缺少默认构造、const 赋值、比较运算和比较不能用于条件的参数。另有正向检查确保不要求多余的复制构造或隐式 bool 转换。

主程序四件套和既有设备生命周期验证仍使用原入口；独立测试不替代硬件验证。

## 版本与接入

本库目前仅建本地 Git 仓库，无远端地址。消费项目应以完整提交号固定版本，不能跟随分支头自动升级；更新本库并提交、运行独立测试后，再显式更新消费项目的版本号并验证。LiDAR_DataACQ 的唯一声明在 `cmake/HandleDependency.cmake`，默认查找与其并列的 HandleCore 仓库，也允许传入其他本地 Git 仓库路径。临时源码覆盖只用于联调，不代表已验证锁定提交。

本库不生成单独的 DLL 或共享库，也不提供设备工厂、Device、Collector、QML 类型登记和系统服务配置。线程关闭、重连及 RX/TX 裁决保持迁入时的契约。

## 已验证环境（2026-10-24）

- Windows x64：MSVC 19.51、Qt 6.11.2，Release /W4 /WX 构建无诊断，CTest 5/5（7.87 秒）。
- Linux x86_64：GCC 16.2.1、Qt 6.12.0、Unix Makefiles，Release 严格警告构建通过，CTest 5/5（5.45 秒）；运行依赖中的 Qt 模块仅 Qt Core。
- 覆盖受控收发、重连和线程退出，不代表 ARM、实际串口、网卡或 USB 设备已经验证。
