# CI 与本地复现

工作流 `.github/workflows/verify.yml` 在未来仓库的 push / pull_request / workflow_dispatch 时运行。当前未创建远端，尚未有 GitHub 托管运行记录。

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

现有测试包含参数约束拒编、受控 I/O、实际库枚举/离线协议、线程归属、重连及同步关闭；不打开真实设备。独立库验证不替代使用方应用回归，不声称 ARM 或实物硬件已经通过。

## 发布前本地验证（2026-10-24）

- Windows x64 / MSVC 19.51、Linux x86_64 / GCC 16.2.1 均使用 Qt 6.8.3，通过同一 `ci/verify.cmake` 执行六组配置。core / serial / pcap / sim-gimbal / sim-card / all 分别通过 5 / 7 / 7 / 7 / 6 / 12 项；每个平台合计 44 次测试执行，包含组合间重复的核心用例。严格警告构建无诊断。
- Linux 当前 Qt 6.12.0 全模块组合再次通过 12/12。Windows 串口使用实际头文件 0.1.1 的无 ConfigVersion 包；Linux 走 pkg-config；Windows Npcap 内 libpcap 1.10.6、Linux libpcap 1.11.0。
- 配置边界探针：无 ConfigVersion 的 0.1.1 头接受，0.1.0 头及缺失版本宏拒绝；Core 和双仿真配置在串口/pkg-config 查找被设为立即失败的环境中仍配置成功，确认未发现禁用模块的依赖。
- actionlint 1.7.12 检查工作流通过。GitHub Actions 尚未实际执行，专用 Windows runner 尚未登记，Ubuntu 24.04 托管环境仍待首次远端运行验证；本次 Linux 实跑环境为 Arch。
- 初次本地脚本遇到多前缀分号拆参、官方 Linux Qt 缺配套 ICU 的问题，分别改用初始缓存传递列表、安装 Qt 同版 ICU 后重跑通过；未修改运行代码或系统 Qt。

完整本地日志保存在维护方的 `2026-10-24-handle-release-prep` 证据目录；迁入阶段的其他结果见 reference.md。库的生产头、既有测试代码、重连/关闭/裁决逻辑本轮未改。
