# HandleCore

C++20 / Qt Core 通信与仿真执行库，供 Windows 和 Linux 的应用层共用。提供同步收发回调、连接重试及同步关闭机制，具体设备业务留在调用方。

基类、Serial 与 SimCard 为头文件目标；Pcap 与 SimGimbal 为静态目标，在库内唯一生成 Qt 信号实现，多个消费库可一起链接。不生成额外 DLL，不依赖 QML、Qt Gui、Quick 或 Qt Test。串口、Pcap、虚拟云台、虚拟采集执行器均为可选模块，**默认全部关闭**。

自有代码采用 [MIT 许可证](LICENSE)，版权署名为周涛（ZHOU TAO）。第三方依赖保持各自许可证，使用与分发边界见 [第三方依赖说明](THIRD_PARTY_NOTICES.md)。

## 模块和依赖

共同要求：**CMake >= 3.28、C++20、Qt Core >= 6.8.3**。6.8.3 是当前维护和验证的支持下限，不表示已经逐一验证更早 Qt 版本。目标平台为 Windows/MSVC、Linux/GCC；ARM 与实物设备仍需对应环境验证。

| CMake 目标 | 启用开关 | 额外依赖 / 职责 |
|---|---|---|
| `Lidar::HandleCore` | 始终提供 | 基类、收发裁决、连接和关闭 |
| `Lidar::Serial` | `HANDLECORE_WITH_SERIAL=ON` | libserialport >= 0.1.1；成帧与协议由应用接入 |
| `Lidar::Pcap` | `HANDLECORE_WITH_PCAP=ON` | Linux libpcap >= 1.10.0；Windows Npcap SDK 及含 libpcap >= 1.10.0 的运行库 |
| `Lidar::SimGimbal` | `HANDLECORE_WITH_SIM_GIMBAL=ON` | 无额外库；两轴运动与进程内命令 |
| `Lidar::SimCard` | `HANDLECORE_WITH_SIM_CARD=ON` | 无额外库；固定窗口计时、强类型输入和合成器执行 |

只有开启对应模块才查找其依赖。Serial 先使用 `libserialport::libserialport` Config 包，找不到时使用 pkg-config；两种入口均检查实际头文件版本，因此兼容没有 ConfigVersion 文件的包。Pcap 在 Linux 通过 pkg-config 检查版本，在 Windows 检查所选 wpcap.dll 内的 libpcap 版本，不能用 Npcap 产品版本替代。

## 独立构建

在仓库根目录执行；Qt 已在搜索路径时可省略 `CMAKE_PREFIX_PATH`：

```text
cmake -S . -B build -DCMAKE_PREFIX_PATH="<Qt 安装前缀>" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

默认只验证 Core。例如只用串口，在首次配置命令后加：

```text
-DHANDLECORE_WITH_SERIAL=ON -DCMAKE_PREFIX_PATH="<Qt 前缀>;<libserialport 前缀>"
```

Linux 也可用 `PKG_CONFIG_PATH` 指向自建依赖。Windows Pcap 将 Npcap SDK 前缀交给 `CMAKE_PREFIX_PATH`；非默认安装可显式指定 `HANDLECORE_PCAP_INCLUDE_DIR`、`HANDLECORE_PCAP_LIBRARY`、`HANDLECORE_PCAP_RUNTIME`。本库不下载依赖或安装驱动。交叉编译 Pcap 的限制见[详细契约](docs/reference.md)。

`BUILD_TESTING=OFF` 关闭独立测试。作为子目录或 FetchContent 依赖时，不创建本库测试，也不改使用方的测试开关。

## 接入项目

先选择模块，再引入库，最后链接所需目标。以下为只启用虚拟采集执行器的示例：

```cmake
include(FetchContent)
set(HANDLECORE_WITH_SIM_CARD ON)
FetchContent_Declare(handlecore
    GIT_REPOSITORY "https://github.com/Lateef-ZHOU/HandleCore.git"
    GIT_TAG        "<已验证的完整提交 SHA>"
)
FetchContent_MakeAvailable(handlecore)

target_link_libraries(my_app PRIVATE Lidar::SimCard)
set_target_properties(my_app PROPERTIES AUTOMOC ON)
```

公开源码位于 [Lateef-ZHOU/HandleCore](https://github.com/Lateef-ZHOU/HandleCore)。将示例中的 GIT_TAG 替换为已验证的完整提交 SHA，不跟随分支头，也不与 `GIT_SHALLOW` 混用。首次配置需要 Git 和访问 GitHub 的网络；拥有源码目录的调用方也可 `add_subdirectory`。

模板型 Handle 的具体末端负责 `Q_OBJECT`、Qt 通知及析构首句 `stop()`；具体 Pcap/虚拟云台类型的 AUTOMOC 由本库负责，不再向消费者传播信号头作为编译源。应用无需包含旧项目的 Device、Collector 或模型。

## 使用边界

- 所属线程调用 `start/send/stop` 并运行事件循环；数据回调在工作线程同步执行，回调所依赖的资源必须活到 `stop()` 返回。
- `connectionChanged(false)` 在所属线程的 `stop()` 促断前同步发出，可发送收尾命令；`isOpen()` 在 stop 返回后才保证关闭。界面通过 `QThread::finished` 回读最终状态，不能把该刷新当作再次撤单。同步通知槽不递归启停或销毁 Handle，详见接口契约。
- 首次打开失败即退出，连接后的 I/O 失败按原机制重连。设备业务配置恢复由协议层或应用负责。
- `RxTime` 直接使用 `std::chrono::system_clock::time_point`；Qt/QML 登记由应用决定。
- Serial 成帧器、SimCard 合成器均按值随连接保存。SimCard 在所属线程取固定窗口快照，工作线程不回访 UI；风场、角度历史和功率谱算法不属于库。
- Pcap 使用单句柄，接收归 worker、实际发送归所属线程；单句柄读写是当前接受的使用约定。当前只处理既定 Ethernet/ARP/IPv4/UDP 格式，不支持 VLAN、其他链路头或分片重组。
- 虚拟设备的 POD 命令仅用于同一进程，不是跨 ABI 的网络协议。

完整参数、线程和模块契约见 [docs/reference.md](docs/reference.md)。

## 自动验证

本地与 GitHub Actions 使用同一个入口：

```text
cmake "-DPROFILES=core;sim-gimbal;sim-card" "-DPREFIX_PATH=<Qt 前缀>" -P ci/verify.cmake
```

提供 `core / serial / pcap / sim-gimbal / sim-card / all` 六种配置，每种使用独立构建目录。省略 PROFILES 时全部执行，需要准备全部依赖；可用 `BUILD_ROOT` 指定输出目录。

GitHub 托管 Windows 验证 Core 与两个仿真模块，托管 Linux 覆盖全部单模块和组合；预装依赖的 Windows runner 验证全部六种配置。后者默认不派发，通过手动输入或仓库变量启用，不在工作流里静默安装 Npcap。详见 [CI 与本地复现](docs/ci.md)，各提交的实际结果以 [Actions 记录](https://github.com/Lateef-ZHOU/HandleCore/actions/workflows/verify.yml) 为准；跳过不等于验证通过。

## 发布状态

本库以 **MIT 许可证公开发布源码**。Qt、libserialport、libpcap/Npcap 由使用环境提供，仓库不附带它们的 SDK、运行库或驱动。

MIT 不代替依赖授权。随产品分发 Qt/libserialport 时须落实 LGPL 或相应授权要求；Linux libpcap 保留 BSD 声明；Windows Npcap 免费许可限制安装数量和再分发，未获授权时由用户从官方自行安装。完整边界见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
