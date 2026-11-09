# HandleCore 维护约束

- 2026-11-02 用户定：连接通知改为 connectionChanged(ResourcePhase)，主动 stop 的 AboutToRelease 同步供收尾发送/撤单；每次建立和撤销连接发 Acquired/Released，完成通知只供回读事实。自动重连不发 AboutToRelease，关闭等待、首连失败退出和重试策略保持，无阶段缓存。

- 2026-10-31 本地诊断已结束（用户定）：用户实采确认主程序收到 7168 字节 UDP 载荷（捕获长度 7210），临时逐包长度日志及端口判断已删除。PcapHandle 恢复已发布 3470a1d 的代码，接收裁决不变；应用恢复该远端固定版本。本轮未推送，验证见 docs/ci.md。

- 本库为 C++20 / Qt Core 库，模板在头文件，枚举元对象由静态目标唯一生成。基类与测试从 LiDAR_DataACQ 8f792a8 迁入；当前接口和构建命令见 README.md。
- 中文沟通；本地提交使用中文 type(scope): 标题，正文记录改动、理由和实际验证。推送需用户明确指示。
- Handle 面向传输与协议，Windows 与 Linux 上层可以复用同一子类；只有实际系统 API 差异才适配，不按业务部署平台拆族。当前包含基类、可选通用串口、Pcap ARP/UDP模块；Pelco-D、风场与谱合成等设备业务仍在应用中。
- 不加 friend、假想需求抽象、额外关闭链或析构状态。RxTime 直接 using 标准库类型；QML 登记归使用方。
- 重连、同步关闭、裁决接口的调整需要用户明确授权。当前同步回调必须在依赖资源清理前退出。
- 使用 Q_EMIT / Q_SIGNALS / Q_SLOTS。模板基类不增加 Q_OBJECT，具体通知由子类提供。
- 上传使用纯虚 onData，由派生 Device 或桥接端点直接实现，不保留外部回调成员/构造参数。内部收包回调工厂与连接 deliver 保持原寿命职责。可复用传输类为非 final、析构不执行预析构；最末端或独占其寿命的所有者必须先 stop/wait，再拆依赖状态。无循环依赖时不强制类外定义。
- 修改后独立构建并跑 CTest；跨平台声明至少分别记录 Windows/MSVC 与 Linux/GCC 的结果，未测 ARM 或硬件不得声称已通过。
- 被其他项目通过 FetchContent 消费时保持测试关闭，仅由顶层独立构建开启测试。不得反向依赖应用头文件。
- 公开发布（2026-10-24 用户定）：远端为 https://github.com/Lateef-ZHOU/HandleCore，公开源码，消费项目仍固定完整提交号。后续推送仍需用户指示。模块默认全部关闭，由各构建入口选择；公共版本声明不得暗开 Serial。支持基线 Qt Core 6.8.3 / CMake 3.28 / C++20，Serial 实际头文件至少 0.1.1，Pcap 至少 libpcap 1.10.0 及 Boost.CircularBuffer 1.88。本地与 CI 共用 `ci/verify.cmake`，四组配置各用独立构建目录；托管 Windows 不安装 Npcap，完整 Windows 验证使用预置环境。README 是接入入口，详细契约及迁移记录见 docs/reference.md，验证范围及远端结果入口见 docs/ci.md。
- 许可证（2026-10-24 用户定）：自有代码、构建、测试和文档采用根目录 LICENSE 的 MIT，用户确认版权署名为周涛（ZHOU TAO），年份 2026，不使用可变昵称。第三方依赖不随之改为 MIT；许可、源码提供与分发边界见 THIRD_PARTY_NOTICES.md。当前不随附外部 SDK、运行库或驱动，不因源码公开而推断取得 Npcap 再分发权。新增第三方材料时保留原有权利声明并更新说明。未获指示不得创建远端或推送。

- 串口层 onRX/onTX 必须保持纯虚。协议成帧器按值随连接保存，允许一块多帧，通过同步交付逐帧进入既有裁决；不把协议帧长、地址或校验知识放入通用库。主动发送与排队回复均在所属主线程串行执行，不另设写互斥，读取独立。
- Pcap（2026-10-24 用户定）：单句柄，worker 接收，实际发送全部归所属主线程；ARP 请求应答由 HandleBase 捕获 conn/r 排队；ARP 回应由 Pcap 捕获 conn/MAC 投递所属线程，统一学习并按序补发全部 pending。PcapConn 扁平保存 deviceMAC/pending，发布前初始化、发布后仅所属线程读写，不使用 ArpState/原子/CAS。2026-11-09 用户定：pending 为容量 32 的 boost::circular_buffer<QByteArray>，满后覆盖最旧一条，学习后从旧到新补发并清空；暂存期间 send 仍返回成功，因为没有 ARP 应答不代表设备断开，上层把失败当断开信号。worker 排队回复前 detach 字节；主线程下发须交可按值保活的 QByteArray，pending 直接隐式共享，不重复 detach。原连接强持到队列项释放；执行时直接通过捕获的原连接发送，不复查当前连接；停止或重连不撤销已排队的回复，Handle 销毁移除队列项。每条回复仅投递一次。单句柄读写为已接受的假设，不新增第二个句柄。模块要求 libpcap >= 1.10.0，Windows 检查 Npcap 内 libpcap 版本；VLAN 等协议扩展暂缓。表单适配留在消费项目，首连失败退出、后续失败重连及同步关闭保持。

- 2026-11-03 用户定：两个仿真 Handle、内部协议、运动模型和测试分别归 SimGimbalDevice / SimCardDevice；本库删除仿真选项和目标，仅保留 Core、Serial、Pcap。


2026-11-04 用户定：Handle 连接入口统一 open/close，内部仍用 QThread::start，外部不使用线程启动口替代连接。Device/Bridge/官邸和验证同步调用名，无兼容薄转发，重连、关闭、阶段通知与裁决保持。Windows Qt 6.11.2/MSVC 独立增量构建、公开头编译及 CTest 6/6 通过（build-virtual-all/open-close-build.log）；本轮未做 Linux/ARM 或硬件验证，未推送。
