# CI 与本地复现

## 2026-11-03 Reject 统一投递所属线程（用户定）

HandleBase 捕获 conn/r，通过绑定自身上下文的 QueuedConnection 直接调用 connSend；排队前 detach 字节，执行前核对原连接身份，禁止停止/重连后的旧回复发送。删除 Pcap 二次投递。队列强持连接至执行或销毁，stop 不排空队列；重连、促断、wait 和裁决种类保持。

Windows/MSVC/Qt 6.11.2 全模块 Release、公开头编译与 CTest 9/9 通过，无 C++ 编译警告。新增/调整检查覆盖主线程实际发送、回复排队前不发送、借用缓冲复用、无二次 TX 封装、停止/重连后丢弃并释放连接及析构移除队列项；串口受控测试确认收包可继续而应答等待主线程处理。证据 build-virtual-all/{build,test}-queued-reply.log。本轮未重跑 Linux、ARM 或实物验证，未推送。

## 2026-11-03 基类声明清理（用户定）

删除 Handle 本层重复的 Q_DISABLE_COPY 和显式默认构造，默认构造由编译器生成，不可复制/移动沿用 QThread/QObject 基类约束；移除未使用的 concepts、utility 头文件。智能指针参数、收发裁决、重连和同步关闭逻辑未改。

Windows/MSVC/Qt 6.11.2：既有 build-virtual-all 全模块 Release 增量构建（含公开头编译）通过，无 C++ 编译警告；CTest 9/9 通过。证据为 build-virtual-all/build-base-cleanup.log、test-base-cleanup.log。本轮未重跑 Linux、ARM 或实物通信；仅本地提交，未推送。

## 2026-11-03 裁决值与参数接口整理

RXRuling/TXRuling 继承 QByteArray，动作使用匿名枚举成员，删除 payload；调用处改为字节在前的聚合初始化。删除 HandleParameters、ParamType/ConnType 及四项只针对旧参数 concept 的负向编译测试，具体传输直接使用实际类型，HandleLike 仅检查公开继承关系。

Windows MSVC / Qt 6.11.2 全模块 Release、独立头编译及 CTest 9/9 通过；Linux GCC / Qt Core 6.8.3 同样 9/9 通过。现有收发裁决、串口/Pcap、仿真、重连与关闭测试保留，运行策略未变。Windows 证据为 build-virtual-all/ruling-final-{build,test}.log；Linux 为 virtual-upload-20261103/ruling-final-HandleCore-{build,test}.log。仅本地验证，无硬件操作或推送。

## 2026-10-31：移除 CDL5 临时接收长度日志

- 用户提供实采截图确认主程序日志为捕获 7210 字节、上交 UDP 载荷 7168 字节；这次是运行中的 Handle 交付日志，而非离线回放或 Wireshark 显示。按要求删除临时日志、55200 端口判断和局部诊断代码，PcapHandle 与已发布 3470a1d 逐字一致；接收逻辑不变。
- Windows / MSVC / Qt 6.11.2：Release 增量构建、单头编译通过，无编译诊断；既有全模块配置 CTest 13/13 通过。证据 build-pcap/build-remove-cdl5-length.log、test-remove-cdl5-length.log。本轮没有再次发起硬件采集，也未运行 Linux；应用恢复已发布远端固定版本，不推送或发 Release。

## 2026-10-31：CDL5 临时接收长度日志

- 用户要求在主程序实际接收路径逐包记录长度。PcapHandle 在 UDP Decode 交付前，对两端端口均为 55200 的连接记录捕获字节数与载荷字节数；不改变报文、时间戳或裁决，不新增配置、计数器或接口。测试后删除此诊断块。
- Windows / MSVC / Qt 6.11.2：既有 build-pcap 的 Release 增量构建、单头编译通过，无编译诊断；该目录当前启用全部模块，CTest 13/13 通过。证据 build-pcap/build-cdl5-length.log、test-cdl5-length.log。
- 本轮未运行 Linux 或实机采集；随后由主程序更新原入口并由用户采集核实日志。仅本地联调，不推送或发布。

工作流 `.github/workflows/verify.yml` 在公开仓库的 push / pull_request / workflow_dispatch 时运行。各提交的状态和日志见 [GitHub Actions](https://github.com/Lateef-ZHOU/HandleCore/actions/workflows/verify.yml)；以下本地验证记录不代替远端运行结果。

## 覆盖与环境

| 环境 | 自动配置 | 依赖来源 |
|---|---|---|
| GitHub Windows 2022 | core、sim-gimbal、sim-card | Qt 6.8.3 MSVC；只下载 qtbase |
| GitHub Ubuntu 24.04 | core、serial、pcap、sim-gimbal、sim-card、all | Qt 6.8.3 qtbase + ICU；仅在需要时装 libserialport-dev / libpcap-dev |
| 预置 Windows runner | 六种配置 | 已准备的 Qt、libserialport、Npcap SDK/运行库 |

Qt 6.8.3 是当前支持下限；日常主项目仍使用自身 Qt 版本。Linux 官方 Qt 包的 ICU 属于工具链运行依赖，不能假定主机系统 ICU 恰好同版。Actions 均固定提交 SHA，权限仅 contents:read，不推送或发布产物；日志/JUnit 作为诊断附件保存。

Windows 全模块 job 不接收 pull_request。启用方式是登记带 `self-hosted, Windows, X64, handlecore-pcap` 标签的 runner，并配置：

- CMake >= 3.28、Visual Studio C++ 工具链在可用环境中。
- 环境变量 `HANDLECORE_CI_PREFIX_PATH`：Qt、libserialport、Npcap SDK 的安装前缀，以分号分隔；系统 Npcap 目录提供运行库。
- 手动运行时勾选 `windows_full`；如需受控分支每次 push 自动执行，将仓库变量 `HANDLECORE_WINDOWS_FULL` 设置为 `true`。

当前不注册 runner、不设置仓库变量、不改变机器驱动。预置条件不满足就不启用该 job，不能将跳过写成 Windows Pcap 验证通过。

## 本地共用入口

以下命令适用于 PowerShell；Linux 的单个前缀同样可以使用。

```text
cmake "-DBUILD_ROOT=<构建目录>" "-DPREFIX_PATH=<Qt 前缀>;<libserialport 前缀>;<Npcap SDK 前缀>" -P ci/verify.cmake
```

只跑某些模块可传 `-DPROFILES=core;serial`，需要 shell 引号保护分号。Linux 自建串口库使用 `PKG_CONFIG_PATH`；无需传 Npcap 前缀。可用 `GENERATOR` / `GENERATOR_PLATFORM` 选择生成器与架构，用 `PARALLEL` 调整构建并行数。

入口逐项配置、构建和 CTest，任一步失败即返回失败；core 不传模块开关，从实际缓存核对四个默认值都为 OFF。其他配置显式关闭无关模块，避免缓存遗留开启额外依赖。每项配置内的 configure.log / build.log / test.log / junit.xml 保存结果。修改模块或依赖布局时使用新的 BUILD_ROOT；不删除现有构建目录。

现有测试包含受控 I/O、实际库枚举/离线协议、线程归属、重连及同步关闭；不打开真实设备。独立库验证不替代使用方应用回归，不声称 ARM 或实物硬件已经通过。

## 发布前本地验证（2026-10-24）

- Windows x64 / MSVC 19.51、Linux x86_64 / GCC 16.2.1 均使用 Qt 6.8.3，通过同一 `ci/verify.cmake` 执行六组配置。core / serial / pcap / sim-gimbal / sim-card / all 分别通过 5 / 7 / 7 / 7 / 6 / 12 项；每个平台合计 44 次测试执行，包含组合间重复的核心用例。严格警告构建无诊断。
- Linux 当前 Qt 6.12.0 全模块组合再次通过 12/12。Windows 串口使用实际头文件 0.1.1 的无 ConfigVersion 包；Linux 走 pkg-config；Windows Npcap 内 libpcap 1.10.6、Linux libpcap 1.11.0。
- 配置边界探针：无 ConfigVersion 的 0.1.1 头接受，0.1.0 头及缺失版本宏拒绝；Core 和双仿真配置在串口/pkg-config 查找被设为立即失败的环境中仍配置成功，确认未发现禁用模块的依赖。
- actionlint 1.7.12 检查工作流通过。GitHub Actions 尚未实际执行，专用 Windows runner 尚未登记，Ubuntu 24.04 托管环境仍待首次远端运行验证；本次 Linux 实跑环境为 Arch。
- 初次本地脚本遇到多前缀分号拆参、官方 Linux Qt 缺配套 ICU 的问题，分别改用初始缓存传递列表、安装 Qt 同版 ICU 后重跑通过；未修改运行代码或系统 Qt。

完整本地日志保存在维护方的 `2026-10-24-handle-release-prep` 证据目录；迁入阶段的其他结果见 reference.md。库的生产头、既有测试代码、重连/关闭/裁决逻辑本轮未改。

## 2026-11-02 资源阶段验证

Windows / MSVC / Qt 6.11.2 全模块 Release 与公开头编译通过，CTest 13/13；新增同步 AboutToRelease 可发送、阶段顺序、重复 stop、旧连接外部份额保活不再开放发送的检查。新增 ResourcePhase/HandleBase 单头编译后重跑核心通过。Linux / GCC 16.2.1 / Qt Core 6.8.3 核心与两项仿真模块构建、CTest 9/9 通过（ArchHFNL 的 resource-phase-20261102/handle-build）。Linux 本轮未启用串口/Pcap；未操作 ARM 或硬件。Windows 证据：build/resource-build-final.log、resource-test.log、resource-test-final.log。


## 2026-11-03 上传虚函数迁移

上传入口改为纯虚 onData；删除外部上传回调存储/构造参数。可复用 Handle 保持抽象，最终接收端必须在派生状态销毁前同步 stop。测试适配器仅存在 tests，库不保留回调兼容入口。

Windows MSVC / Qt 6.11.2：build-virtual-all 全模块 Release、公开头 /W4 /WX、CTest 13/13 通过。Linux GCC / Qt Core 6.8.3：ArchHFNL 的 virtual-upload-20261103/build-HandleCore 全模块构建及 CTest 13/13 通过。覆盖重连、关闭、串口一块多帧、Pcap 排队发送与虚拟执行；未运行 ARM 或真实硬件。运行逻辑不变；仅本地提交，尚未推送。

## 2026-11-03 删除回调类型别名（用户定）

删除 Sink、DataCallback，连接循环、串口成帧器、Pcap C 回调及仿真交付直接声明实际 std::function 类型；持有方式、同步借用与关闭顺序不变。Windows/MSVC/Qt 6.11.2 全模块 Release、公开头编译和 CTest 9/9 通过；Linux/GCC/Qt Core 6.8.3 全模块构建与 CTest 9/9 通过。Windows 日志 build-virtual-all/build-callback-alias.log 与 CTest LastTest.log；Linux 日志 virtual-upload-20261103/build-HandleCore/callback-alias-{build,test}.log。未操作硬件或更新 ARM。
