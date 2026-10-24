# HandleCore

独立的 C++20 / Qt Core 通信库。首版从 LiDAR_DataACQ 提交 `8f792a8d976089e3eba8edd7ae0fbedbf32e72f6` 的 `include/Device/Base/HandleBase.h` 原样迁入，构建与测试一并迁移。此处维护通信基类、通用串口及可选以太网 ARP/UDP 模块；Pelco-D 和设备业务仍在应用仓库。

Handle 按通信方式和协议复用，不按 Windows 设备或 Linux 设备划分。Linux 桥接器和 Windows 官邸均可直接使用同一具体 Handle；串口、UDP、激光器、温控或相机传输只有实际操作系统 API 差异才需要适配。设备业务配置恢复由子类或上层负责，不进入基类。通用串口已迁入；ARM 和实物设备尚未验证。

## 依赖与使用

- CMake 3.28 或以上、C++20、Qt Core；核心目标不依赖 QML、Gui、Quick、Qt Test、Npcap、libserialport 或应用 Device/Collector。可选串口目标额外依赖 libserialport，Pcap 模块额外依赖 libpcap >= 1.10.0（Windows 使用 Npcap）。
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

独立入口编译所选功能的 `tests/`。作为其他项目的子目录时不添加本库测试、不修改使用方的测试开关。主程序通过相同目标引用基类，仍保留自身原有的界面及传输依赖。

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

## 通用串口（2026-10-24）

启用 `HANDLECORE_WITH_SERIAL=ON` 后提供 `Lidar::Serial`，包含 `<SerialHandle.h>` / `<SerialPort.h>`，传递核心目标及 libserialport 链接依赖。默认关闭串口，保留 Core 独立构建。先查找 libserialport 的 CMake Config 包，没有时使用 pkg-config 的 libserialport >= 0.1.1；不自动下载或安装依赖。

```text
cmake -S . -B build-serial -DHANDLECORE_WITH_SERIAL=ON -DCMAKE_PREFIX_PATH="<Qt 前缀>;<libserialport 前缀>"
cmake --build build-serial --config Release --parallel 4
ctest --test-dir build-serial -C Release --output-on-failure
```

Linux 使用单配置生成器时加 `-DCMAKE_BUILD_TYPE=Release`；自建 libserialport 可通过 `PKG_CONFIG_PATH=<前缀>/lib/pkgconfig` 查找。

- `SerialHandle<Framer>` 实现四个连接接口；`onRX/onTX` 仍纯虚，具体协议必须重写。连接及日志通知同样由具体末端提供 Qt 信号，末端析构首先 stop。
- `Framer` 是可默认构造的值对象，实现 `feed(const QByteArray &, RxTime, const DataCallback &)`。只在收包线程访问，随连接创建和销毁；一次读取同步交出 0..N 帧，之后每帧单独进入原有 RX 裁决。不得保存 deliver 或输入视图，不引入跨线程解析队列。
- 协议成帧器在消费项目定义。库不包含 Pelco-D、设备地址、命令或校验规则；Windows 官邸和 Linux 桥接器可使用同一协议 Handle。
- `SerialParams` 接收端口名/路径、正整数波特率、数据位、停止位、校验、流控及正的写超时。常用波特率枚举是 UI 选项，不限制连接值；默认 9600/8N1、无流控、500 ms。驱动不支持的设置使打开失败。0 写超时被拒绝，避免在本接口中变成无限等待。
- 收块上限 256 字节、读取超时 100 ms 保持；RxTime 在读取返回后取得，是软件观测值。写口按连接互斥，允许收包线程 Reject 回包与所属线程主动发送，读取独立。短写/失败返 false，不整条自动重发；完整写入不代表远端设备执行成功。
- 首次失败、每秒重连、促断标志及 stop/wait 继续由原有机制负责；HandleBase.h 未修改。资源及成帧器随连接释放，不增加析构状态或第二条关闭链。

新增 `serial_probe` 用真实 libserialport 头文件和测试链接替身覆盖生产连接代码、两种协议纯虚约束、多帧/半帧、参数、短写与失败、并发命令/Reject、重连丢弃旧尾部、回调期间停止和半开资源释放。`serial_link_probe` 真正链接安装的库并枚举端口，不打开硬件。

本轮 Windows/MSVC/Qt 6.11.2 构建零诊断，CTest 7/7（9.12 秒）；Linux x86_64/GCC/Qt 6.12.0 构建零诊断，CTest 7/7（6.74 秒）。Linux 的 libserialport 0.1.2 从官方源码单独构建至用户缓存前缀，不改系统软件包；第三方构建有其版本宏重定义警告，消费本库的严格警告构建通过。实物拔插、ARM 驱动与设备动作不在受控测试覆盖范围内。

## 可选 Pcap 模块（2026-10-24）

`HANDLECORE_WITH_PCAP=ON` 提供头文件目标 `Lidar::Pcap`。`PcapHandle.h` 管理单个 pcap_t、连接参数快照、ARP 学习及 UDP 收发；`PcapProtocol.h` 提供 Ethernet/ARP/IPv4/UDP 字节值对象和 BPF 谓词；`PcapInterfaces.h` 枚举实际 pcap 接口及 MAC。调用方启用 AUTOMOC 并链接目标，目标自带具体 Handle 的 Qt 信号头。不依赖 Qt Gui/QML，不包含表单下标、默认值或设备型号。

```text
cmake -S . -B build-pcap -DHANDLECORE_WITH_PCAP=ON -DCMAKE_PREFIX_PATH="<Qt 前缀>;<Npcap SDK 前缀>"
cmake --build build-pcap --config Release --parallel 4
ctest --test-dir build-pcap -C Release --output-on-failure
```

Linux 使用 `-DCMAKE_BUILD_TYPE=Release`，通过 pkg-config 强制 `libpcap>=1.10.0`；不需要 Npcap 前缀。Windows 需要 Npcap SDK 及已安装的驱动/运行库，CMake 按目标架构选择导入库，从 `HANDLECORE_PCAP_RUNTIME` 指向的实际 wpcap.dll 读取 **libpcap** 版本，不能把 Npcap 产品版本当作 libpcap 版本。自动查找会使用前缀内 Bin 或系统 Npcap 目录，必要时显式指定运行库路径；部署须使用满足相同要求的运行库。Windows 交叉编译无法执行目标 DLL 时，需依据目标运行库资料显式传 `HANDLECORE_PCAP_VERSION`，仍要求 >= 1.10.0。ARM 交叉编译尚未验证。

所属线程运行事件循环并调用 start/send/stop。worker 只收包、解析和更新连接内 ARP 原子快照；所有 pcap_sendpacket 在所属线程执行。主动 send 同步返回实际发送结果；收包裁决产生的 ARP 应答及学习后补发通过绑定 Handle 上下文的排队调用发送，队列接受不代表已发到设备。队列深拷贝完整报文，仅弱持原连接；旧连接失效或 Handle 销毁便丢弃，不续命连接，不查找新的当前连接。

单个 pcap_t 的并发接收/发送是当前用户接受的使用约定，**不宣称 libpcap 对它提供跨平台并发保证**，没有新增第二个句柄、写线程或重试层。MAC 未知时只保留最新一条待发 UDP，每次主动发送触发一次 ARP 探测；新连接重新建立学习状态。首次打开失败即退出，后续接收/重开失败每秒重连，stop 的促断及同步等待保持；HandleBase.h 和 Serial 实现未改。打开、配置、激活、过滤器及发送失败均记录具体原因，不引入永久错误分类。

当前只支持普通 Ethernet、固定 20 字节 IPv4 头、非分片 UDP 和既有 ARP 格式。VLAN、其他链路头、IP 选项及分片重组暂不扩展；实际网卡应返回 Ethernet 格式，不使用 Linux `any` 等 cooked capture 接口。Windows 枚举结合 pcap 名单和系统适配器名称/MAC，Linux 从 AF_PACKET 取得以太网 MAC。协议扩展时须同时更新识别、BPF 与载荷偏移。UDP 载荷读口拒绝不足头长的帧；只读迭代器避免 GCC 优化后对 Qt 空数组哨兵的越界告警，不做协议形式扩展。

验证：Windows x64 / MSVC / Qt 6.11.2 / Npcap 1.86（libpcap 1.10.6）与 Linux x86_64 / GCC 16.2.1 / Qt 6.12.0 / libpcap 1.11.0 均严格警告构建零诊断，完整 CTest **9/9** 通过。`pcap_probe` 使用 C API 链接替身驱动生产 Handle，验证主线程发送、ARP 学习与最新报文补发、旧回包跨停止/重连/析构的丢弃、RxTime、截断包、发送失败、半开资源释放及等待在途回调；`pcap_link_probe` 真正链接系统库，枚举网卡并离线编译/执行 BPF，不打开或发送到实物网卡。三个新增公共头分别独立编译。

Linux 另以 libpcap 1.9.1 的 pkg-config 元数据确认配置拒绝，关闭模块后 Core 独立构建和 5/5 测试通过；Pcap 探针运行时只依赖 Qt Core，不引入 QML/Gui。以上不是 ARM、实物断连重连或单句柄驱动并发实测。
