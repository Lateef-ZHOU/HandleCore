# CI 与本地复现

## 2026-11-09：MAC 未知时暂存多条报文（用户定）

PcapConn 的 pending 由单个 QByteArray 改为容量 32 的 `boost::circular_buffer<QByteArray>`：MAC 未知时按到达顺序暂存，满后覆盖最旧一条；ARP 回应在所属线程学习 MAC 后从旧到新逐条补发并清空。send 仍返回成功，不把未应答 ARP 当作设备断开；每次主动发送仍发一次 ARP 探测。Pcap 模块新增 Boost.CircularBuffer 依赖，按 RayshiningDevice 的方式复用已有目标或已装包，否则下载固定 SHA256 的 Boost 1.88.0。

Windows/MSVC/Qt 6.11.2 全模块（build-virtual-all）Release 构建零诊断，CTest 6/6 通过（4.56 秒）。pcap_probe 改为验证两条暂存按序补发、主线程未处理学习前追加的报文一并补发、重复回应不重复发，新增 33 条溢出只丢最旧一条。Boost 使用主程序 build/_deps 中同版本源码。日志：build-virtual-all/pending-ring-configure.log、pending-ring-build.log。未测 Linux/ARM 或实物网卡，未推送。

## 2026-11-05：协议排队发送裁决（用户定）

TXRuling 新增 Queued：协议已接管排队，send 返回接受，不再调用 connSend；实际写入失败由协议使用现有 connSend 日志报告。SerialHandle 的 final connSend 开放给派生协议定时发送，未增加转发方法。测试替身记录写入时刻供温控节流验证，生产收包、重连和关闭逻辑保持。

Windows/MSVC/Qt 6.11.2 全模块 Release、公开头构建和 CTest 6/6 通过；Queued 返回成功且不立即走通用 connSend 的负向检查通过。温控集成验证覆盖首条立即、100 ms 间隔和尾条冷却、关闭/重连清队列、短写/错误日志及析构。Linux Qt 6.8.3 的温控专项 1/1、Bridge 4/4 和 ARM64 板上替身测试通过，详细部署和实物只读查询记录在 BridgeCore/VALIDATION.md。未推送。

证据：build-virtual-all/queued-build.log、Testing/Temporary/LastTest.log；相邻 TCM232Device/build/build-paced-final.log。

## 2026-11-03：清理串口测试写阻塞残留（用户定）

仅删除测试替身的 `blockWrites`、`writesBlocked` 和模拟发送内的条件等待；发送侧锁改用 `lock_guard`。接收仍用条件变量等待，保留 `writes()` 及短写/失败注入。生产代码、发送线程、重连和关闭逻辑均未改，不更新应用固定依赖或运行包。

Windows/MSVC/Qt 6.11.2 增量构建通过：HandleCore 串口 CTest 2/2（1.31 秒），激光器 serial-delivery/device/telemetry/commands 4/4（2.52 秒）。Linux/GCC 16.2.1/Qt Core 6.8.3：复用该替身的 BridgeCore 增量构建、CTest 4/4（2.98 秒）通过。均为受控测试，未连接实物、未更新板卡；仅本地提交，未推送。

证据：本库 `build-virtual-all/{build,test}-remove-write-block.log`，RayshiningDevice 的 `build/flatten-tests/{build,test}-remove-write-block.log`，ArchHFNL 的 `~/.cache/lidar-flatten-includes-20261103/bridge-remove-write-block-{build,test}.log`。

## 2026-11-03：仿真 Handle 迁回官邸（用户定）

删除仿真模块开关、构建目标和 CI 配置；两个 Handle、内部协议、运动模型及对应测试迁入各自官邸。运行头与迁移前逐文件核对一致；测试主体不变。历史验证下文保留当时范围，当前 profile 为 core / serial / pcap / all。

Windows Qt 6.11.2：剩余全模块与公开头构建通过，CTest 6/6；迁移后的虚拟云台 2/2、虚拟卡 1/1。Linux GCC 16.2.1 / Qt Core 6.8.3：Core 1/1、云台 2/2、虚拟卡 1/1，构建无编译警告；ldd 确认两个仿真探针的 Qt 依赖仅 Core。Linux 本轮未启用串口/Pcap。证据 Windows build-virtual-all/*-sim-move.log、两官邸 build-handle；Linux ~/.cache/lidar-sim-handle-move-20261103/*-{build,test}.log。未推送，未操作板卡或硬件。

## 2026-11-03：删除无调用方的串口选项接口（用户定）

删除 availableBaudRates、availableDataBits、availableStopBits、availableParities、optionTexts、toString(BaudRate) 及无用 QStringList 头。保留 Bridge 使用的数据位、停止位、校验方式转换，保留枚举、端口枚举及连接参数。清理 UI 档位表旧注释；不改通信逻辑。

Windows/MSVC/Qt 6.11.2 全模块增量 Release、公开头编译及 CTest 9/9（12.51 秒）通过，无 C++ 编译诊断。证据 build-virtual-all/{build,test}-serial-options.log。未重编 Bridge，已核对其三处调用与保留签名一致；未测 Linux、ARM 或硬件，未推送。


## 2026-11-03：删除串口发送互斥（用户定）

删除 SerialConn::send 的 lock_guard、writeGate_ 成员与 mutex 头文件。主动发送及 HandleBase 排队的 Reject 回复均由所属主线程串行执行，不再保留连接级写锁；收包、重连、关闭和发送结果判断保持。当前契约与维护约束已同步，下方写互斥记录为历史。

Windows/MSVC/Qt 6.11.2 增量全模块 Release、公开头编译与 CTest 9/9（12.51 秒）通过，无 C++ 编译诊断。既有串口验证覆盖排队回复不在 worker 发送、发送串行、重连及关闭。证据 build-virtual-all/{build,test}-serial-no-lock.log。未测 Linux、ARM 或硬件，未推送。


## 2026-11-03：串口发送只返回成功与否（用户定）

删除 send 的错误输出参数；完整写入返回 true，短写和底层错误返回 false，由 Handle 统一记录一次“串口发送失败”。不返回或缓存错误详情，替代下方上一轮的详细诊断接口；收发、重连与关闭逻辑保持。

Windows/MSVC/Qt 6.11.2 全模块 Release、公开头编译及 CTest 9/9（12.52 秒）通过，无 C++ 编译诊断；覆盖完整写入、短写、零字节超时、底层错误及失败日志次数。证据 build-virtual-all/{build,test}-serial-bool.log。未测 Linux、ARM 或硬件，未推送。


## 2026-11-03：串口连接层判断发送结果（用户定）

SerialConn::write 改为 bool send(data, error)，完整写入才返回 true；负错误码与短写诊断由连接层生成，经 QString 输出参数交 Handle 发一次日志。connSend 只检查空连接、调用 send 并报告失败，删除字节数二次判断；不增加错误缓存、日志回调或发送重试。loop/breakLoop、写互斥和关闭逻辑保持。

Windows/MSVC/Qt 6.11.2 全模块 Release、公开头编译和 CTest 9/9（12.50 秒）通过，无 C++ 编译诊断。发送验证覆盖完整写入、短写、零字节超时和负错误码，并检查每项失败只记录一次。证据 build-virtual-all/{build,test}-serial-send.log。未测 Linux、ARM 或硬件，未推送。


## 2026-11-03：串口接收循环命名整理（用户定）

删除 SerialHandle 的 Base 同义别名及继承构造声明，沿用隐式默认构造；SerialConn 的接收循环统一命名 loop，与 breakLoop 对应。connLoop 同步调用点已更新，接收、成帧、发送及关闭行为不变。

Windows/MSVC/Qt 6.11.2 全模块 Release、公开头编译和 CTest 9/9（12.52 秒）通过，无 C++ 编译诊断，包含串口半帧/多帧、排队回复、重连与关闭用例。证据 build-virtual-all/{build,test}-serial-loop.log。未测 Linux、ARM 或硬件，未推送。


## 2026-11-03：资源枚举合入 HandleBase（用户定）

ResourcePhase 及 Qt namespace/元类型登记移入 HandleBase.h 的模板外部，删除独立头与构建引用；静态 Core 目标仍唯一生成元对象，枚举名称、数值及阶段行为保持。单头编译只保留 HandleBase，核心探针新增命名空间与四个枚举值的元对象检查。

Windows/MSVC/Qt 6.11.2 全模块 Release、公开头编译与 CTest 9/9（12.54 秒）通过。证据 build-virtual-all/{build,test}-phase-header.log。未测 Linux、ARM 或硬件，未推送。


## 2026-11-03：主线程下发保持隐式共享（用户定）

删除 pending.detach()；主线程下发须提供可按 QByteArray 值语义保活的数据，pending 直接共享。worker 借用接收缓冲跨线程投递时取得所有权，HandleBase 的 Reject 排队前 r.detach() 保留；不增加所有权检测或状态。替代下方上一轮 pending 接受临时借用源的契约。

Windows/MSVC/Qt 6.11.2 全模块 Release、公开头编译与 CTest 9/9（12.56 秒）通过，无 C++ 编译诊断。pending 验证检查发送后源数组仍共享、随后源数组改写不影响补发；worker 回复借用缓冲复用的既有验证继续通过。证据 build-virtual-all/{build,test}-pending-share.log。未测 Linux、ARM 或硬件，未推送。


## 2026-11-03 ARP 状态归所属线程（用户定）

PcapConn 扁平保存 deviceMAC 与 pending，删除 ArpState、原子共享指针和 CAS。连接发布前初始化 MAC 种子；发布后发送、MAC 学习和 pending 补发均在所属线程。worker 仅解析 ARP 回应并捕获原连接/MAC 投递；未知 MAC 时保留最新报文并 detach 字节，学习后取空补发，重复回应不重复发送。原连接保活、停止/重连后原连接队列继续执行及析构移除队列项的裁定保持，HandleBase 不改。

Windows/MSVC/Qt 6.11.2 全模块 Release、公开头编译与 CTest 9/9（12.54 秒）通过，无 C++ 编译诊断。新增受控验证覆盖回应已收但主线程尚未处理时替换 pending、借用源复用、重复回应、种子 MAC、旧学习事件跨停止/重连不污染新连接以及析构取消。证据 build-virtual-all/{build,test}-arp-owner.log。本轮未测 Linux、ARM 或实物，未推送。

## 2026-11-03 删除 Pcap 启动转发（用户定）

删除 PcapHandle::start，直接继承基类入口；运行中拒绝日志放入 Handle::start 的既有 isRunning 分支，删除要求手动断开才能更换参数的错误提示。不改自动重启、重连和关闭逻辑。

Windows/MSVC/Qt 6.11.2 全模块 Release、公开头编译及既有 CTest 9/9（11.32 秒）通过，无 C++ 编译警告；证据 build-virtual-all/{build,test}-start-entry.log。本轮未重跑 Linux、ARM 或实物验证，仅本地提交。

## 2026-11-03 排队回复直接使用捕获连接（用户定）

删除 Reject 队列中的当前连接身份判断，保留 r.detach()、conn/r 按值捕获和所属线程 connSend。停止或重连不撤销已排队回复，仍向被保活的原连接尝试发送；Handle 销毁移除队列项。不改重连、关闭或裁决流程。此裁定替代下方上一轮的旧回复丢弃策略。

Windows/MSVC/Qt 6.11.2 全模块 Release、公开头编译及 CTest 9/9（11.33 秒）通过，无 C++ 编译警告。Pcap 受控验证检查停止重开及自动重连后，回复在所属线程使用原连接及原字节、随后释放连接；析构取消排队调用的验证保留。既有借用缓冲复用测试继续通过。证据 build-virtual-all/{build,test}-captured-reply.log；本轮未重跑 Linux、ARM 或硬件，未推送。

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
| GitHub Windows 2022 | core | Qt 6.8.3 MSVC；只下载 qtbase |
| GitHub Ubuntu 24.04 | core、serial、pcap、all | Qt 6.8.3 qtbase + ICU；仅在需要时装 libserialport-dev / libpcap-dev |
| 预置 Windows runner | 四种配置 | 已准备的 Qt、libserialport、Npcap SDK/运行库 |

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

## 2026-11-03 移除无调用方的 IPv4 文本转换

删除 IPv4::toString；MAC::toString 仍供校验验证使用，地址解析、网卡枚举与协议逻辑保持。Windows build-virtual-all 增量构建成功，CTest 9/9（12.58 秒），日志 build-network-ui.log / test-network-ui.log；主程序构建与八秒冒烟通过。未运行 Linux 或硬件验证。

## 2026-11-03 串口纯虚契约去重

删除 SerialHandle 本层重复的 onRX/onTX 纯虚声明，直接继承 Handle 的约束；不改变收发、重连和关闭。Windows build-virtual-all 增量构建成功，CTest 9/9（12.54 秒）。现有 MissingRX/MissingTX 编译期断言通过，证明协议遗漏实现仍为抽象类。证据：build-virtual-all/build-serial-contract.log、test-serial-contract.log。本轮未测 Linux 或硬件。


## 2026-11-03 串口声明整理（用户定）

删除 SerialConn 重复复制禁令、两个测试的无效构造继承声明，纠正串口描述用途注释。补齐不可复制构造、复制赋值、移动构造、移动赋值四项 static_assert；收发、重连和关闭代码不变。

Windows/MSVC/Qt 6.11.2：build-virtual-all Release 增量构建成功，CTest 6/6（4.86 秒）。主程序以相邻源码覆盖接入，增量 Release 构建与八秒离屏冒烟通过，仅既有 FT_New_Face 提示。证据：build-virtual-all/{build,test}-serial-cleanup.log，LiDAR_DataACQ/build/build-serial-cleanup.log、smoke-serial-cleanup.json。未测 Linux、ARM 或实物，本轮不推送。


2026-11-04 用户定：Handle 连接入口统一 open/close，内部仍用 QThread::start，外部不使用线程启动口替代连接。Device/Bridge/官邸和验证同步调用名，无兼容薄转发，重连、关闭、阶段通知与裁决保持。Windows Qt 6.11.2/MSVC 独立增量构建、公开头编译及 CTest 6/6 通过（build-virtual-all/open-close-build.log）；本轮未做 Linux/ARM 或硬件验证，未推送。
## 2026-11-04：命令列表 send 重载

Windows/MSVC/Qt 6.11.2 独立增量构建、公开头编译及 CTest 6/6 通过（4.87 秒）。新增空列表零发送、顺序发送、TX 拒绝与底层发送失败均短路的验证，现有串口/Pcap、重连和关闭回归通过。证据：build-virtual-all/send-list-build.log、Testing/Temporary/LastTest.log。未测 Linux/ARM 或实物，未推送。
